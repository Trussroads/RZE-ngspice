/***********************************************************************
 HiSIM v1.1.0
 File: hsm1cvtest.c of HiSIM v1.1.0

 Copyright (C) 2002 STARC

 June 30, 2002: developed by Hiroshima University and STARC
 June 30, 2002: posted by Keiichi MORIKAWA, STARC Physical Design Group
***********************************************************************/

/*
 * Modified by Paolo Nenzi 2002
 * ngspice integration
 */

#include "ngspice.h"
#include "cktdefs.h"
#include "hsm1def.h"
#include "trandefs.h"
#include "const.h"
#include "devdefs.h"
#include "sperror.h"
#include "suffix.h"

int HSM1convTest(GENmodel *inModel, CKTcircuit *ckt)
{
  HSM1model *model = (HSM1model*)inModel;
  HSM1instance *here;
  double delvbd, delvbs, delvds, delvgd, delvgs, vbd, vbs, vds;
  double cbd, cbhat, cbs, cd, cdhat, tol, vgd, vgdo, vgs;

  /*  loop through all the HSM1 device models */
  for ( ; model != NULL; model = model->HSM1nextModel ) {
    /* loop through all the instances of the model */
    for ( here = model->HSM1instances; here != NULL ;
	  here = here->HSM1nextInstance ) {
	  
	  
      if (here->HSM1owner != ARCHme)
              continue;
  
      vbs = model->HSM1_type * 
	(*(ckt->CKTrhsOld+here->HSM1bNode) - 
	 *(ckt->CKTrhsOld+here->HSM1sNodePrime));
      vgs = model->HSM1_type *
	(*(ckt->CKTrhsOld+here->HSM1gNode) - 
	 *(ckt->CKTrhsOld+here->HSM1sNodePrime));
      vds = model->HSM1_type * 
	(*(ckt->CKTrhsOld+here->HSM1dNodePrime) - 
	 *(ckt->CKTrhsOld+here->HSM1sNodePrime));
      vbd = vbs - vds;
      vgd = vgs - vds;
      vgdo = *(ckt->CKTstate0 + here->HSM1vgs) - 
	*(ckt->CKTstate0 + here->HSM1vds);
      delvbs = vbs - *(ckt->CKTstate0 + here->HSM1vbs);
      delvbd = vbd - *(ckt->CKTstate0 + here->HSM1vbd);
      delvgs = vgs - *(ckt->CKTstate0 + here->HSM1vgs);
      delvds = vds - *(ckt->CKTstate0 + here->HSM1vds);
      delvgd = vgd - vgdo;

      cd = here->HSM1_ids - here->HSM1_ibd;
      if ( here->HSM1_mode >= 0 ) {
	cd += here->HSM1_isub;
	cdhat = cd - here->HSM1_gbd * delvbd 
	  + (here->HSM1_gmbs + here->HSM1_gbbs) * delvbs
	  + (here->HSM1_gm + here->HSM1_gbgs) * delvgs
	  + (here->HSM1_gds + here->HSM1_gbds) * delvds;
      }
      else {
	cdhat = cd + (here->HSM1_gmbs - here->HSM1_gbd) * delvbd 
	  + here->HSM1_gm * delvgd - here->HSM1_gds * delvds;
      }

      /*
       *  check convergence
       */
      if ( here->HSM1_off == 0  || !(ckt->CKTmode & MODEINITFIX) ) {
	tol = ckt->CKTreltol * MAX(fabs(cdhat), fabs(cd)) + ckt->CKTabstol;
	if ( fabs(cdhat - cd) >= tol ) {
	  ckt->CKTnoncon++;
	  return(OK);
	} 
	cbs = here->HSM1_ibs;
	cbd = here->HSM1_ibd;
	if ( here->HSM1_mode >= 0 ) {
	  cbhat = cbs + cbd - here->HSM1_isub + here->HSM1_gbd * delvbd 
	    + (here->HSM1_gbs - here->HSM1_gbbs) * delvbs
	    - here->HSM1_gbgs * delvgs - here->HSM1_gbds * delvds;
	}
	else {
	  cbhat = cbs + cbd - here->HSM1_isub 
	    + here->HSM1_gbs * delvbs
	    + (here->HSM1_gbd - here->HSM1_gbbs) * delvbd 
	    - here->HSM1_gbgs * delvgd + here->HSM1_gbds * delvds;
	}
	tol = ckt->CKTreltol * 
	  MAX(fabs(cbhat), fabs(cbs + cbd - here->HSM1_isub)) + ckt->CKTabstol;
	if ( fabs(cbhat - (cbs + cbd - here->HSM1_isub)) > tol ) {
	  ckt->CKTnoncon++;
	  return(OK);
	}
      }
    }
  }
  return(OK);
}
