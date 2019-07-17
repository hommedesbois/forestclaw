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

#include "annulus_user.h"

#include <fclaw2d_clawpatch.h>
#include <fclaw2d_clawpatch_options.h>

#include <fc2d_clawpack46_options.h>
#include <fc2d_clawpack46.h>


/* ------------- Create the domain --------------------- */
static
fclaw2d_domain_t* create_domain(sc_MPI_Comm mpicomm, 
                                fclaw_options_t* fclaw_opt, 
                                user_options_t* user)
{
    /* Used locally */
    double pi = M_PI;
    double rotate[2];
    int mi, mj, a,b;

    /* Mapped, multi-block domain */
    p4est_connectivity_t     *conn = NULL;
    fclaw2d_domain_t         *domain;
    fclaw2d_map_context_t    *cont = NULL, *brick = NULL;

    /* ---------------------------------------------------------------
       Mapping geometry
       --------------------------------------------------------------- */
    mi = fclaw_opt->mi;
    mj = fclaw_opt->mj;
    rotate[0] = pi*fclaw_opt->theta/180.0;
    rotate[1] = pi*fclaw_opt->phi/180.0;
    a = fclaw_opt->periodic_x;
    b = fclaw_opt->periodic_y;

    /* Annulus */
    conn = p4est_connectivity_new_brick(mi,mj,a,b);
    brick = fclaw2d_map_new_brick(conn,mi,mj);
    cont = fclaw2d_map_new_annulus(brick,fclaw_opt->scale,fclaw_opt->shift,
                                   rotate,user->beta, user->theta, user->mapping);

    domain = fclaw2d_domain_new_conn_map (mpicomm, fclaw_opt->minlevel, conn, cont);

    fclaw2d_domain_list_levels(domain, FCLAW_VERBOSITY_ESSENTIAL);
    fclaw2d_domain_list_neighbors(domain, FCLAW_VERBOSITY_DEBUG);
    return domain;
}

static
void run_program(fclaw2d_global_t* glob)
{
    /* ---------------------------------------------------------------
       Set domain data.
       --------------------------------------------------------------- */
    fclaw2d_domain_data_new(glob->domain);

    /* Initialize virtual table for ForestClaw */
    fclaw2d_vtables_initialize(glob);

    fc2d_clawpack46_solver_initialize();

    annulus_link_solvers(glob);

    fclaw2d_initialize(glob);
    fclaw2d_run(glob);
    fclaw2d_finalize(glob);
}


int
main (int argc, char **argv)
{
    int first_arg;
    fclaw_app_t *app;
    fclaw_exit_type_t vexit;

    /* Options */
    sc_options_t                *options;
    user_options_t              *user_opt;
    fclaw_options_t             *fclaw_opt;
    fclaw2d_clawpatch_options_t *clawpatch_opt;
    fc2d_clawpack46_options_t   *claw46_opt;

    fclaw2d_global_t         *glob;
    fclaw2d_domain_t         *domain;
    sc_MPI_Comm mpicomm;

    int retval;

    /* Initialize application */
    app = fclaw_app_new (&argc, &argv, NULL);

    /* Register packages */
    fclaw_opt                  = fclaw_options_register(app, "fclaw_options.ini");
    clawpatch_opt  = fclaw2d_clawpatch_options_register(app, "fclaw_options.ini");
    claw46_opt       = fc2d_clawpack46_options_register(app, "fclaw_options.ini");
    user_opt                 = annulus_options_register(app, "fclaw_options.ini");

    /* Read configuration file(s) */
    options = fclaw_app_get_options (app);
    retval = fclaw_options_read_from_file(options);
    vexit =  fclaw_app_options_parse (app, &first_arg,"fclaw_options.ini.used");

    if (!retval & !vexit)
    {
        
        /* Options have been checked and are valid */
        mpicomm = fclaw_app_get_mpi_size_rank (app, NULL, NULL);
        domain = create_domain(mpicomm, fclaw_opt, user_opt);

        glob = fclaw2d_global_new();
        fclaw2d_global_store_domain(glob, domain);

        fclaw2d_options_store            (glob, fclaw_opt);
        fclaw2d_clawpatch_options_store  (glob, clawpatch_opt);
        fc2d_clawpack46_options_store    (glob, claw46_opt);
        annulus_options_store            (glob, user_opt);

        run_program(glob);

        fclaw2d_global_destroy (glob);
    }

    fclaw_app_destroy (app);

    return 0;
}
