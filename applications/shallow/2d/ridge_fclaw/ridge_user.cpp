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

#include "amr_forestclaw.H"
#include "amr_waveprop.H"
#include "ridge_user.H"

#ifdef __cplusplus
extern "C"
{
#if 0
}
#endif
#endif

void ridge_link_solvers(fclaw2d_domain_t *domain)
{
    fclaw2d_solver_functions_t* sf = get_solver_functions(domain);

    // Link methods to call for each ClawPatch.
    sf->use_single_step_update = fclaw_true;
    sf->use_mol_update = fclaw_false;

    sf->f_patch_setup              = &ridge_patch_setup;
    sf->f_patch_initialize         = &ridge_patch_initialize;
    sf->f_patch_physical_bc        = &ridge_patch_physical_bc;
    sf->f_patch_single_step_update = &ridge_patch_single_step_update;

    // This sets any static functions needed to construct data associated with
    // the waveprop solver.
    amr_waveprop_link_to_clawpatch();
}


void ridge_link_regrid_functions(fclaw2d_domain_t* domain)
{
    fclaw2d_regrid_functions_t *rf = get_regrid_functions(domain);

    rf->f_patch_tag4refinement = &ridge_tag4refinement;
    rf->f_patch_tag4coarsening = &ridge_tag4coarsening;
    rf->f_patch_interpolate2fine = &ridge_interpolate2fine;
}

void ridge_problem_setup(fclaw2d_domain_t* domain)
{
    /* Setup any fortran common blocks for general problem
       and any other general problem specific things that only needs
       to be done once. */
    set_maptype_();
    amr_waveprop_setprob(domain);
}


void ridge_patch_setup(fclaw2d_domain_t *domain,
                       fclaw2d_patch_t *this_patch,
                       int this_block_idx,
                       int this_patch_idx)
{
    /* --------------------------------------------------------------------
       The standard Clawpack call here is a call to setaux. In this example,
       however, we want to include more paramters in the call than is typically
       taken in by setaux. So we modify the call here.  We have to then
       get all the parameters and input arguments for our own call.  If we
       just wanted to make a standard call, we could do it one line :

       amr_waveprop_setaux(domain, this_patch, this_block_idx, this_patch_idx);

       The code below can be cut and paste and used for other similar examples.
       -------------------------------------------------------------------- */

    // Set the index of the current block, in case this info is needed by setaux.
    set_block_(&this_block_idx);


    /* --------------------------------------------------------------------- */
    // Get any global parameters that we might need :
    const amr_options_t *gparms = get_domain_parms(domain);

    int mx = gparms->mx;
    int my = gparms->my;
    int maxmx = mx;
    int maxmy = my;
    int mbc = gparms->mbc;
    int maxlevel = gparms->maxlevel;
    int refratio = gparms->refratio;
    int level = this_patch->level;

    /* --------------------------------------------------------------------- */
    // Get parameters specific to current patch
    ClawPatch *cp = get_clawpatch(this_patch);
    double xlower = cp->xlower();
    double ylower = cp->ylower();
    double dx = cp->dx();
    double dy = cp->dy();

    /* ------------------------------------------------------------------- */
    // allocate space for the aux array
    amr_waveprop_define_auxarray(domain,cp);

    /* ------------------------------------------------------------------- */
    // Include additional metric terms not typically passed to 'setaux'
    double *xnormals = cp->xface_normals();
    double *ynormals = cp->yface_normals();
    double *xtangents = cp->xface_tangents();
    double *ytangents = cp->yface_tangents();
    double *surfnormals = cp->surf_normals();

    /* ----------------------------------------------------------- */
    // Get newly created aux array
    double *aux;
    int maux;
    amr_waveprop_get_auxarray(domain,cp,&aux,&maux);

    /* ------------------------------------------------------------------- */
    // Make call to our own version of setaux.  Here, we have included metric
    // terms that we need for SWE.

    setaux_ridge_(maxmx,maxmy,mbc,mx,my,xlower,ylower,dx,dy,maux,aux,
                  xnormals,ynormals,
                  xtangents,ytangents,
                  surfnormals,
                  level,maxlevel,refratio);
}

void ridge_patch_initialize(fclaw2d_domain_t *domain,
                            fclaw2d_patch_t *this_patch,
                            int this_block_idx,
                            int this_patch_idx)
{
    // Use standard clawpack call to qinit.
    amr_waveprop_qinit(domain,this_patch,this_block_idx,this_patch_idx);
}


void ridge_patch_physical_bc(fclaw2d_domain *domain,
                             fclaw2d_patch_t *this_patch,
                             int this_block_idx,
                             int this_patch_idx,
                             double t,
                             double dt,
                             fclaw_bool intersects_bc[])
{
    // The sphere doesn't have physical boundary conditions.
}


double ridge_patch_single_step_update(fclaw2d_domain_t *domain,
                                      fclaw2d_patch_t *this_patch,
                                      int this_block_idx,
                                      int this_patch_idx,
                                      double t,
                                      double dt)
{
    // Standard call to b4step2, step2 (amrclaw version) and src2
    // At the end of this call, this patch has been updated with a
    // new solution.
    amr_waveprop_b4step2(domain,this_patch,this_block_idx,this_patch_idx,t,dt);

    double maxcfl = amr_waveprop_step2(domain,this_patch,this_block_idx,
                                       this_patch_idx,t,dt);

    amr_waveprop_src2(domain,this_patch,this_block_idx,this_patch_idx,t,dt);

    return maxcfl;
}

void ridge_patch_output(fclaw2d_domain_t *domain, fclaw2d_patch_t *this_patch,
                        int this_block_idx, int this_patch_idx,
                        int iframe,int num,int matlab_level)
{
    // In case this is needed by the setaux routine
    set_block_(&this_block_idx);

    /* ----------------------------------------------------------- */
    // Global parameters
    const amr_options_t *gparms = get_domain_parms(domain);
    int mx = gparms->mx;
    int my = gparms->my;
    int mbc = gparms->mbc;
    int meqn = gparms->meqn;

    /* ----------------------------------------------------------- */
    // Patch specific parameters
    ClawPatch *cp = get_clawpatch(this_patch);
    double xlower = cp->xlower();
    double ylower = cp->ylower();
    double dx = cp->dx();
    double dy = cp->dy();

    /* ------------------------------------------------------------ */
    // Pointers needed to pass to Fortran
    double* q = cp->q();

    // Other input arguments
    int maxmx = mx;
    int maxmy = my;

    /* ----------------------------------------------------------- */
    // Get newly created aux array
    double *aux;
    int maux;
    amr_waveprop_get_auxarray(domain,cp,&aux,&maux);

    /* ------------------------------------------------------------- */
    // This opens a file for append.  Now, the style is in the 'clawout' style.
    write_qfile_geo_(maxmx,maxmy,meqn,mbc,mx,my,xlower,ylower,dx,dy,q,
                     iframe,num,matlab_level,this_block_idx,maux,aux);
}


fclaw_bool ridge_tag4refinement(fclaw2d_domain_t *domain,
                                fclaw2d_patch_t *this_patch,
                                int this_block_idx,
                                int this_patch_idx, int initflag)
{
    // In case this is needed by the setaux routine
    set_block_(&this_block_idx);

    /* ----------------------------------------------------------- */
    // Global parameters
    const amr_options_t *gparms = get_domain_parms(domain);
    int mx = gparms->mx;
    int my = gparms->my;
    int mbc = gparms->mbc;
    int meqn = gparms->meqn;

    /* ----------------------------------------------------------- */
    // Patch specific parameters
    ClawPatch *cp = get_clawpatch(this_patch);
    double xlower = cp->xlower();
    double ylower = cp->ylower();
    double dx = cp->dx();
    double dy = cp->dy();

    /* ------------------------------------------------------------ */
    // Pointers needed to pass to Fortran
    double* q = cp->q();

    /* ----------------------------------------------------------- */
    double *aux;
    int maux;
    amr_waveprop_get_auxarray(domain,cp,&aux,&maux);


    /* ----------------------------------------------------------- */
    int tag_patch;
    int level = this_patch->level;
    ridge_tag4refinement_(mx,my,mbc,meqn,xlower,ylower,dx,dy,
                          q,level,maux,aux,initflag,tag_patch);
    return tag_patch == 1;
}

fclaw_bool ridge_tag4coarsening(fclaw2d_domain_t *domain,
                                fclaw2d_patch_t *this_patch,
                                int this_blockno,
                                int this_patchno)
{
    // In case this is needed by the setaux routine
    set_block_(&this_blockno);

    /* ----------------------------------------------------------- */
    // Global parameters
    const amr_options_t *gparms = get_domain_parms(domain);
    int mx = gparms->mx;
    int my = gparms->my;
    int mbc = gparms->mbc;
    int meqn = gparms->meqn;

    /* ----------------------------------------------------------- */
    // Patch specific parameters
    ClawPatch *cp = get_clawpatch(this_patch);
    double xlower = cp->xlower();
    double ylower = cp->ylower();
    double dx = cp->dx();
    double dy = cp->dy();

    /* ------------------------------------------------------------ */
    // Pointers needed to pass to Fortran
    double* q = cp->q();

    double *aux;
    int maux;
    amr_waveprop_get_auxarray(domain,cp,&aux,&maux);

    /* ----------------------------------------------------------- */
    int tag_patch;  // == 0 or 1

    /*
    int level = this_patch->level; // Level of coarsened patch
    int initflag = 0;
    ridge_tag4refinement_(mx,my,mbc,meqn,xlower,ylower,dx,dy,
                          q,level,maux,aux,initflag,tag_patch);
    */

    ridge_tag4coarsening_(mx,my,mbc,meqn,xlower,ylower,
                          dx,dy,q,maux,aux,tag_patch);


    return tag_patch == 0;
}

void ridge_interpolate2fine(fclaw2d_domain_t* domain, fclaw2d_patch_t *coarse_patch,
                                fclaw2d_patch_t* fine_patch,
                                int this_blockno, int coarse_patchno,
                                int fine_patchno, int igrid)

{
    const amr_options_t* gparms = get_domain_parms(domain);

    int mx = gparms->mx;
    int my = gparms->my;
    int mbc = gparms->mbc;
    int meqn = gparms->meqn;
    int refratio = gparms->refratio;

    ClawPatch *cp_coarse = get_clawpatch(coarse_patch);
    double *qcoarse = cp_coarse->q();

    double *aux_coarse;
    int maux;
    amr_waveprop_get_auxarray(domain,cp_coarse,&aux_coarse,&maux);

    ClawPatch *cp_fine = get_clawpatch(fine_patch);
    double *qfine = cp_fine->q();

    double *aux_fine;
    amr_waveprop_get_auxarray(domain,cp_fine,&aux_fine,&maux);

    // Use linear interpolation with limiters.
    interpolate_geo_(mx,my,mbc,meqn,qcoarse,qfine,maux,aux_coarse,
                     aux_fine, p4est_refineFactor, refratio,igrid);
    if (gparms->manifold)
    {
        double *areacoarse = cp_coarse->area();
        double *areafine = cp_fine->area();

        fixcapaq2_(mx, my, mbc, meqn, qcoarse, qfine, areacoarse, areafine,
                   p4est_refineFactor, refratio, igrid);
    }
}


#ifdef __cplusplus
#if 0
{
#endif
}
#endif
