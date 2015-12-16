/*
Copyright (c) 2012 Carsten Burstedde, Donna Calhoun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <fclaw2d_forestclaw.h>
#include <fclaw2d_clawpatch.hpp>

#include <fclaw2d_advance.hpp>
#include <fclaw2d_regrid.h>
#include <fclaw2d_output.h>
#include <fclaw2d_diagnostics.h>

#ifdef __cplusplus
extern "C"
{
#if 0
}
#endif
#endif


#include "fclaw_math.h"

/*  -----------------------------------------------------------------
    Time stepping
    -- saving time steps
    -- restoring time steps
    -- Time stepping, based on when output files should be created.
    ----------------------------------------------------------------- */

static
void cb_restore_time_step(fclaw2d_domain_t *domain,
                          fclaw2d_patch_t *this_patch,
                          int this_block_idx,
                          int this_patch_idx,
                          void *user)
{
    ClawPatch *this_cp = fclaw2d_clawpatch_get_cp(this_patch);

    /* Copy most current time step data to grid data. (m_griddata <== m_griddata_last) */
    this_cp->restore_step();
}

static
void restore_time_step(fclaw2d_domain_t *domain)
{
    fclaw2d_domain_iterate_patches(domain,cb_restore_time_step,(void *) NULL);
}

static
void cb_save_time_step(fclaw2d_domain_t *domain,
                       fclaw2d_patch_t *this_patch,
                       int this_block_idx,
                       int this_patch_idx,
                       void *user)
{
    ClawPatch *this_cp = fclaw2d_clawpatch_get_cp(this_patch);

    /* Copy grid data (m_griddata) on each patch to temporary storage
       (m_griddata_tmp <== m_griddata); */
    this_cp->save_step();
}

static
void save_time_step(fclaw2d_domain_t *domain)
{
    fclaw2d_domain_iterate_patches(domain,cb_save_time_step,(void *) NULL);
}


/* -------------------------------------------------------------------------------
   Output style 1
   Output times are at times [0,dT, 2*dT, 3*dT,...,Tfinal], where dT = tfinal/nout
   -------------------------------------------------------------------------------- */
static void outstyle_0(fclaw2d_domain_t **domain)
{
    const amr_options_t *gparms;
    int iframe;

    gparms = get_domain_parms(*domain);


    iframe = 0;
    fclaw2d_output_frame(*domain,iframe);

    if (gparms->run_diagnostics)
    {
        fclaw2d_run_diagnostics(*domain);
    }

    /* Here is where we might include a call to a static solver, for, say,
       an elliptic solve. The initial grid might contain, for example, a
       right hand side. */
#if 0
    vt.time_indepdent_solve(domain);

    if (gparms->run_diagnostics)
    {
        fclaw2d_diagnostics_run(domain);
    }

    iframe++;
    fclaw2d_output_frame(*domain,iframe);
#endif

}


/* -------------------------------------------------------------------------------
   Output style 1
   Output times are at times [0,dT, 2*dT, 3*dT,...,Tfinal], where dT = tfinal/nout
   -------------------------------------------------------------------------------- */
static
void outstyle_1(fclaw2d_domain_t **domain)
{
    int iframe = 0;
    fclaw2d_output_frame(*domain,iframe);

    const amr_options_t *gparms = get_domain_parms(*domain);

    double final_time = gparms->tfinal;
    int nout = gparms->nout;
    double initial_dt = gparms->initial_dt;
    double dt_minlevel = initial_dt;

    double t0 = 0;

    double dt_outer = (final_time-t0)/double(nout);
    double t_curr = t0;

    for(int n = 0; n < nout; n++)
    {
        double tstart = t_curr;
        double tend = tstart + dt_outer;
        int n_inner = 0;
        while (t_curr < tend)
        {
            subcycle_manager time_stepper;
            time_stepper.define(*domain,gparms,t_curr,dt_minlevel);

            fclaw2d_domain_set_time(*domain,t_curr);

            /* In case we have to reject this step */
            if (!gparms->use_fixed_dt)
            {
                save_time_step(*domain);
            }

            if (gparms->run_diagnostics)
            {
                /* Get current domain data since it may change during
                   regrid. */
                fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data(*domain);
                fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CHECK]);

                fclaw2d_run_diagnostics(*domain);

                fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CHECK]);
            }

            /* Use the tolerance to make sure we don't take a tiny time
               step just to hit 'tend'.   We will take a slightly larger
               time step now (dt_cfl + tol) rather than taking a time step
               of 'dt_minlevel' now, followed a time step of only 'tol' in
               the next step.  Of course if 'tend - t_curr > dt_minlevel',
               then dt_minlevel doesn't change. */
            double tol = 1e-2*dt_minlevel;
            fclaw_bool took_small_step = false;
            fclaw_bool took_big_step = false;
            double dt_minlevel_desired = dt_minlevel;
            if (!gparms->use_fixed_dt)
            {
                double small_step = tend-t_curr-dt_minlevel;
                if (small_step  < tol)
                {
                    dt_minlevel = tend - t_curr;  // <= 'dt_minlevel + tol'
                    if (small_step < 0)
                    {
                        /* We have (tend-t_curr) < dt_minlevel, and
                           we have to take a small step to hit tend */
                        took_small_step = true;
                    }
                    else
                    {
                        /* Take a bigger step now to avoid small step
                           in next time step. */
                        took_big_step = true;
                    }
                }
            }

            double maxcfl_step = advance_all_levels(*domain, &time_stepper);

            fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);
            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CFL]);
            maxcfl_step = fclaw2d_domain_global_maximum (*domain, maxcfl_step);
            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CFL]);


            double tc = time_stepper.sync_time();
            fclaw_global_productionf("Level %d (%2d) step %5d : dt = %12.3e; maxcfl (step) = " \
                                     "%8.3f; Final time = %12.4f\n",    \
                                     time_stepper.user_minlevel(),
                                     time_stepper.local_minlevel(),
                                     n_inner,dt_minlevel,
                                     maxcfl_step, tc);

            if (maxcfl_step > gparms->max_cfl)
            {
                fclaw_global_infof("   WARNING : Maximum CFL exceeded; "    \
                                   "retaking time step\n");
                if (!gparms->use_fixed_dt)
                {
                    restore_time_step(*domain);

                    /* Modify dt_level0 from step used. */
                    dt_minlevel = dt_minlevel*gparms->desired_cfl/maxcfl_step;

                    /* Got back to start of loop, without incrementing
                       step counter or time level */
                    continue;
                }
            }

            if (!gparms->use_fixed_dt)
            {
                if (took_small_step)
                {
                    fclaw_global_infof("   WARNING : Took small time step which was " \
                                       "%6.1f%% of desired dt.\n",
                                       100.0*dt_minlevel/dt_minlevel_desired);
                }
                if (took_big_step)
                {
                    fclaw_global_infof("   WARNING : Took big time step which was " \
                                       "%6.1f%% of desired dt.\n",
                                       100.0*dt_minlevel/dt_minlevel_desired);
                }


                /* New time step, which should give a cfl close to the
                   desired cfl. */
                double dt_new = dt_minlevel*gparms->desired_cfl/maxcfl_step;
                if (!took_small_step)
                {
                    dt_minlevel = dt_new;
                }
                else
                {
                    /* use time step that would have been used had we
                       not taken a small step */
                }
            }
            n_inner++;
            t_curr += dt_minlevel;

            if (gparms->regrid_interval > 0)
            {
                if (n_inner % gparms->regrid_interval == 0)
                {
                    fclaw_global_infof("regridding at step %d\n",n);
                    fclaw2d_regrid(domain);
                }
            }
            else
            {
                /* Use a static grid */
            }
        }

        /* Output file at every outer loop iteration */
        fclaw2d_domain_set_time(*domain,t_curr);
        iframe++;
        fclaw2d_output_frame(*domain,iframe);
    }
}

#if 0
static void outstyle_2(fclaw2d_domain_t **domain)
{
    // Output time at specific time steps.
}
#endif

static
void outstyle_3(fclaw2d_domain_t **domain)
{
    int iframe = 0;
    fclaw2d_output_frame(*domain,iframe);

    const amr_options_t *gparms = get_domain_parms(*domain);
    double initial_dt = gparms->initial_dt;

    double t0 = 0;
    double dt_minlevel = initial_dt;
    fclaw2d_domain_set_time(*domain,t0);
    int nstep_outer = gparms->nout;
    int nstep_inner = gparms->nstep;
    int nregrid_interval = gparms->regrid_interval;
    if (!gparms->subcycle)
    {
        if (gparms->global_time_stepping)
        {
            /* Multiply nout/nstep by 2^(maxlevel-minlevel) so that
               a given nout/nstep pair works for both subcycled
               and non-subcycled cases */
            int mf = pow_int(2,gparms->maxlevel-gparms->minlevel);
            nstep_outer *= mf;
#if 0
            nstep_inner *= mf;  /* Only produce nout/nstep output files */
            nregrid_interval *= mf;
#endif
        }
    }

    int n = 0;
    double t_curr = t0;
    while (n < nstep_outer)
    {
        /* Count only coarse grid time steps */
        subcycle_manager time_stepper;
        time_stepper.define(*domain,gparms,t_curr,dt_minlevel);

        /* In case we have to reject this step */
        if (!gparms->use_fixed_dt)
        {
            save_time_step(*domain);
        }

        if (gparms->run_diagnostics)
        {
            /* Get current domain data since it may change during regrid */
            fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);

            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CHECK]);

            fclaw2d_run_diagnostics(*domain);

            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CHECK]);
        }

        double maxcfl_step = advance_all_levels(*domain, &time_stepper);

        /* This is a collective communication - everybody needs to wait here. */
        fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);
        fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CFL]);
        maxcfl_step = fclaw2d_domain_global_maximum (*domain, maxcfl_step);
        fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CFL]);

        double dt_step = time_stepper.global_step();
        double tc = time_stepper.sync_time();
        fclaw_global_productionf("Level %d (%d-%d) step %5d : dt = %12.3e; maxcfl (step) = " \
                                 "%8.3f; Final time = %12.4f\n",
                                 time_stepper.user_minlevel(),
                                 time_stepper.local_minlevel(),
                                 time_stepper.local_maxlevel(),
                                 n+1,
                                 dt_step,maxcfl_step, tc);

        if (maxcfl_step > gparms->max_cfl)
        {
            fclaw_global_productionf("   WARNING : Maximum CFL exceeded; retaking time step\n");

            if (!gparms->use_fixed_dt)
            {
                restore_time_step(*domain);

                /* Modify dt_level0 from step use */
                dt_minlevel = dt_minlevel*gparms->desired_cfl/maxcfl_step;

                /* Got back to start of loop without incrementing step counter or
                   current time. */
                continue;
            }
        }

        /* We are happy with this time step */
        t_curr = tc;
        fclaw2d_domain_set_time(*domain,t_curr);

        /* New time step, which should give a cfl close to the desired cfl. */
        if (!gparms->use_fixed_dt)
        {
            dt_minlevel = dt_minlevel*gparms->desired_cfl/maxcfl_step;
        }

        n++;  /* Increment outer counter */

        if (gparms->regrid_interval > 0)
        {
            if (n % nregrid_interval == 0)
            {
                fclaw_global_infof("regridding at step %d\n",n);
                fclaw2d_regrid(domain);
            }
        }

        if (n % nstep_inner == 0)
        {
            iframe++;
            fclaw2d_output_frame(*domain,iframe);
        }
    }
}

#if 0

static
void outstyle_4(fclaw2d_domain_t **domain)
{
    /* Write out an initial time file */
    int iframe = 0;
    fclaw2d_output_frame(*domain,iframe);

    const amr_options_t *gparms = get_domain_parms(*domain);
    double initial_dt = gparms->initial_dt;

    int expand_to_fine = 1;  /* Make this an option */
    int nstep_outer = gparms->nout;
    int nstep_inner = gparms->nstep;
    int nregrid_interval = gparms->regrid_interval;
    if (!gparms->subcycle)
    {
        /* Multiply nout/nstep by 2^(maxlevel-minlevel) so that
           a given nout/nstep pair works for both subcycled
           and non-subcycled cases */
        int mf = pow_int(2,gparms->maxlevel-gparms->minlevel);
        nstep_outer *= mf;
        if (!expand_to_fine)
        {
            nstep_inner *= mf;
            nregrid_interval *= mf;
        }
    }

    double t0 = 0;
    double dt_minlevel_stable = initial_dt;
    fclaw2d_domain_set_time(*domain,t0);

    int n = 0;
    double t_curr = t0;
    while (n < nstep_outer)
    {
        subcycle_manager time_stepper;
        time_stepper.define(*domain,gparms,t_curr,dt_minlevel_stable);

        /* In case we have to reject this step */
        if (!gparms->use_fixed_dt)
        {
            save_time_step(*domain);
        }

        if (gparms->run_diagnostics)
        {
            /* Get current domain data since it may change during regrid */
            fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);

            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CHECK]);

            fclaw2d_run_diagnostics(*domain);

            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CHECK]);
        }

        double maxcfl_step = advance_all_levels(*domain, &time_stepper);

        /* This is a collective communication - everybody needs to wait here. */
        fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);
        fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CFL]);
        maxcfl_step = fclaw2d_domain_global_maximum (*domain, maxcfl_step);
        fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CFL]);

        double dt_step = time_stepper.dt(gparms->minlevel);
        double tc = time_stepper.sync_time();
        FCLAW_ASSERT(fabs(tc-(t_curr+dt_step)) < 1e-12);
        fclaw_global_productionf("Level %2d (%2d) step %5d : dt = %12.3e; maxcfl (step) = " \
                                 "%8.3f; Final time = %12.4f\n",
                                 time_stepper.user_minlevel(),
                                 time_stepper.local_minlevel(),
                                 n+1,dt_step,maxcfl_step, tc);

        if (maxcfl_step > gparms->max_cfl)
        {
            fclaw_global_productionf("   WARNING : Maximum CFL exceeded; retaking time step\n");

            if (!gparms->use_fixed_dt)
            {
                restore_time_step(*domain);

                /* Modify dt_level0 from step use */
                dt_minlevel_stable = dt_minlevel_stable*gparms->desired_cfl/maxcfl_step;

                /* Got back to start of loop without incrementing step counter or
                   current time. */
                continue;
            }
        }

        /* We are happy with this time step */
        t_curr = tc;
        fclaw2d_domain_set_time(*domain,t_curr);

        /* New time step, which should give a cfl close to the desired cfl. */
        if (!gparms->use_fixed_dt)
        {
            dt_minlevel_stable = dt_minlevel_stable*gparms->desired_cfl/maxcfl_step;
        }

        n++;  /* Increment outer counter */

        if (nregrid_interval > 0)
        {
            if (n % nregrid_interval == 0)
            {
                fclaw_global_infof("regridding at step %d\n",n);
                fclaw2d_regrid(domain);
            }
        }


        if (n % nstep_inner == 0)
        {
            iframe++;
            fclaw2d_output_frame(*domain,iframe);
        }

    }
}
#endif

static
void outstyle_4(fclaw2d_domain_t **domain)
{
    /* Write out an initial time file */
    int iframe = 0;
    fclaw2d_output_frame(*domain,iframe);

    const amr_options_t *gparms = get_domain_parms(*domain);
    double initial_dt = gparms->initial_dt;
    int nstep_outer = gparms->nout;
    int nstep_inner = gparms->nstep;

    // assume fixed dt that is stable for all grids.

    double t0 = 0;
    double t_curr = t0;
    fclaw2d_domain_set_time(*domain,t_curr);
    int n = 0;
    while (n < nstep_outer)
    {
        subcycle_manager time_stepper;
        double dt_minlevel = initial_dt;
        time_stepper.define(*domain,gparms,t_curr,dt_minlevel);

        if (gparms->run_diagnostics)
        {
            /* Get current domain data since it may change during regrid */
            fclaw2d_domain_data_t *ddata = fclaw2d_domain_get_data(*domain);
            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_CHECK]);

            fclaw2d_run_diagnostics(*domain);

            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_CHECK]);
        }



        advance_all_levels(*domain, &time_stepper);

        fclaw_global_productionf("Level %d step %5d : dt = %12.3e; Final time = %16.6e\n",
                                 time_stepper.user_minlevel(),
                                 n+1,dt_minlevel, t_curr+dt_minlevel);

        t_curr += dt_minlevel;
        n++;

        fclaw2d_domain_set_time(*domain,t_curr);

        if (gparms->regrid_interval > 0)
        {
            if (n % gparms->regrid_interval == 0)
            {
                fclaw_global_infof("regridding at step %d\n",n);

                fclaw2d_regrid(domain);
            }
        }
        else
        {
            /* Only use the initial grid */
        }

        if (n % nstep_inner == 0)
        {
            iframe++;
            fclaw2d_output_frame(*domain,iframe);
        }
    }
}


/* ------------------------------------------------------------------
   Public interface
   ---------------------------------------------------------------- */

void fclaw2d_run(fclaw2d_domain_t **domain)
{

    const amr_options_t *gparms = get_domain_parms(*domain);

    switch (gparms->outstyle)
    {
    case 0:
        outstyle_0(domain);
        break;
    case 1:
        outstyle_1(domain);
        break;
    case 2:
        fclaw_global_essentialf("Outstyle %d not implemented yet\n", gparms->outstyle);
        exit(0);
    case 3:
        outstyle_3(domain);
        break;
    case 4:
        outstyle_4(domain);
        break;
    default:
        fclaw_global_essentialf("Outstyle %d not implemented yet\n", gparms->outstyle);
        exit(0);
    }
}

#ifdef __cplusplus
#if 0
{
#endif
}
#endif
