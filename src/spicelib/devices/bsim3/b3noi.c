/**********
Copyright 1999 Regents of the University of California.  All rights reserved.
Author: 1995 Gary W. Ng and Min-Chie Jeng.
Author: 1997-1999 Weidong Liu.
File:  b3noi.c
**********/

#include "ngspice.h"
#include <stdio.h>
#include <math.h>
#include "bsim3def.h"
#include "cktdefs.h"
#include "iferrmsg.h"
#include "noisedef.h"
#include "suffix.h"
#include "const.h"  /* jwan */

/*
 * BSIM3noise (mode, operation, firstModel, ckt, data, OnDens)
 *    This routine names and evaluates all of the noise sources
 *    associated with MOSFET's.  It starts with the model *firstModel and
 *    traverses all of its insts.  It then proceeds to any other models
 *    on the linked list.  The total output noise density generated by
 *    all of the MOSFET's is summed with the variable "OnDens".
 */

/*
 Channel thermal and flicker noises are calculated based on the value
 of model->BSIM3noiMod.
 If model->BSIM3noiMod = 1,
    Channel thermal noise = SPICE2 model
    Flicker noise         = SPICE2 model
 If model->BSIM3noiMod = 2,
    Channel thermal noise = BSIM3 model
    Flicker noise         = BSIM3 model
 If model->BSIM3noiMod = 3,
    Channel thermal noise = SPICE2 model
    Flicker noise         = BSIM3 model
 If model->BSIM3noiMod = 4,
    Channel thermal noise = BSIM3 model
    Flicker noise         = SPICE2 model
 */

extern void   NevalSrc();
extern double Nintegrate();

double
StrongInversionNoiseEval(vgs, vds, model, here, freq, temp)
double vgs, vds, freq, temp;
BSIM3model *model;
BSIM3instance *here;
{
struct bsim3SizeDependParam *pParam;
double cd, esat, DelClm, EffFreq, N0, Nl, Vgst;
double T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, Ssi;

    pParam = here->pParam;
    cd = fabs(here->BSIM3cd);
    if (vds > here->BSIM3vdsat)
    {   esat = 2.0 * pParam->BSIM3vsattemp / here->BSIM3ueff;
	T0 = ((((vds - here->BSIM3vdsat) / pParam->BSIM3litl) + model->BSIM3em)
	   / esat);
        DelClm = pParam->BSIM3litl * log (MAX(T0, N_MINLOG));
    }
    else 
        DelClm = 0.0;
    EffFreq = pow(freq, model->BSIM3ef);
    T1 = CHARGE * CHARGE * 8.62e-5 * cd * temp * here->BSIM3ueff;
    T2 = 1.0e8 * EffFreq * model->BSIM3cox
       * pParam->BSIM3leff * pParam->BSIM3leff;
    Vgst = vgs - here->BSIM3von;
    N0 = model->BSIM3cox * Vgst / CHARGE;
    if (N0 < 0.0)
	N0 = 0.0;
    Nl = model->BSIM3cox * (Vgst - MIN(vds, here->BSIM3vdsat)) / CHARGE;
    if (Nl < 0.0)
	Nl = 0.0;

    T3 = model->BSIM3oxideTrapDensityA
       * log(MAX(((N0 + 2.0e14) / (Nl + 2.0e14)), N_MINLOG));
    T4 = model->BSIM3oxideTrapDensityB * (N0 - Nl);
    T5 = model->BSIM3oxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

    T6 = 8.62e-5 * temp * cd * cd;
    T7 = 1.0e8 * EffFreq * pParam->BSIM3leff
       * pParam->BSIM3leff * pParam->BSIM3weff;
    T8 = model->BSIM3oxideTrapDensityA + model->BSIM3oxideTrapDensityB * Nl
       + model->BSIM3oxideTrapDensityC * Nl * Nl;
    T9 = (Nl + 2.0e14) * (Nl + 2.0e14);

    Ssi = T1 / T2 * (T3 + T4 + T5) + T6 / T7 * DelClm * T8 / T9;
    return Ssi;
}

int
BSIM3noise (mode, operation, inModel, ckt, data, OnDens)
int mode, operation;
GENmodel *inModel;
CKTcircuit *ckt;
register Ndata *data;
double *OnDens;
{
register BSIM3model *model = (BSIM3model *)inModel;
register BSIM3instance *here;
struct bsim3SizeDependParam *pParam;
char name[N_MXVLNTH];
double tempOnoise;
double tempInoise;
double noizDens[BSIM3NSRCS];
double lnNdens[BSIM3NSRCS];

double vgs, vds, Slimit;
 double T1, T10, T11;
double Ssi, Swi;

int i;

    /* define the names of the noise sources */
    static char *BSIM3nNames[BSIM3NSRCS] =
    {   /* Note that we have to keep the order */
	".rd",              /* noise due to rd */
			    /* consistent with thestrchr definitions */
	".rs",              /* noise due to rs */
			    /* in BSIM3defs.h */
	".id",              /* noise due to id */
	".1overf",          /* flicker (1/f) noise */
	""                  /* total transistor noise */
    };

    for (; model != NULL; model = model->BSIM3nextModel)
    {    for (here = model->BSIM3instances; here != NULL;
	      here = here->BSIM3nextInstance)
	 {    pParam = here->pParam;
	      switch (operation)
	      {  case N_OPEN:
		     /* see if we have to to produce a summary report */
		     /* if so, name all the noise generators */

		      if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
		      {   switch (mode)
			  {  case N_DENS:
			          for (i = 0; i < BSIM3NSRCS; i++)
				  {    (void) sprintf(name, "onoise.%s%s",
					              here->BSIM3name,
						      BSIM3nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **) NULL);
				       /* we've added one more plot */
			          }
			          break;
		             case INT_NOIZ:
			          for (i = 0; i < BSIM3NSRCS; i++)
				  {    (void) sprintf(name, "onoise_total.%s%s",
						      here->BSIM3name,
						      BSIM3nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **) NULL);
				       /* we've added one more plot */

			               (void) sprintf(name, "inoise_total.%s%s",
						      here->BSIM3name,
						      BSIM3nNames[i]);
                                       data->namelist = (IFuid *) trealloc(
					     (char *) data->namelist,
					     (data->numPlots + 1)
					     * sizeof(IFuid));
                                       if (!data->namelist)
					   return(E_NOMEM);
		                       (*(SPfrontEnd->IFnewUid)) (ckt,
			                  &(data->namelist[data->numPlots++]),
			                  (IFuid) NULL, name, UID_OTHER,
					  (void **)NULL);
				       /* we've added one more plot */
			          }
			          break;
		          }
		      }
		      break;
	         case N_CALC:
		      switch (mode)
		      {  case N_DENS:
		              NevalSrc(&noizDens[BSIM3RDNOIZ],
				       &lnNdens[BSIM3RDNOIZ], ckt, THERMNOISE,
				       here->BSIM3dNodePrime, here->BSIM3dNode,
				       here->BSIM3drainConductance);

		              NevalSrc(&noizDens[BSIM3RSNOIZ],
				       &lnNdens[BSIM3RSNOIZ], ckt, THERMNOISE,
				       here->BSIM3sNodePrime, here->BSIM3sNode,
				       here->BSIM3sourceConductance);

                              switch( model->BSIM3noiMod )
			      {  case 1:
			         case 3:
			              NevalSrc(&noizDens[BSIM3IDNOIZ],
				               &lnNdens[BSIM3IDNOIZ], ckt, 
					       THERMNOISE, here->BSIM3dNodePrime,
				               here->BSIM3sNodePrime,
                                               (2.0 / 3.0 * fabs(here->BSIM3gm
				               + here->BSIM3gds
					       + here->BSIM3gmbs)));
				      break;
			         case 2:
			         case 4:
		                      NevalSrc(&noizDens[BSIM3IDNOIZ],
				               &lnNdens[BSIM3IDNOIZ], ckt,
					       THERMNOISE, here->BSIM3dNodePrime,
                                               here->BSIM3sNodePrime,
					       (here->BSIM3ueff
					       * fabs(here->BSIM3qinv
					       / (pParam->BSIM3leff
					       * pParam->BSIM3leff))));
				      break;
			      }
		              NevalSrc(&noizDens[BSIM3FLNOIZ], (double*) NULL,
				       ckt, N_GAIN, here->BSIM3dNodePrime,
				       here->BSIM3sNodePrime, (double) 0.0);

                              switch( model->BSIM3noiMod )
			      {  case 1:
			         case 4:
			              noizDens[BSIM3FLNOIZ] *= model->BSIM3kf
					    * exp(model->BSIM3af
					    * log(MAX(fabs(here->BSIM3cd),
					    N_MINLOG)))
					    / (pow(data->freq, model->BSIM3ef)
					    * pParam->BSIM3leff
				            * pParam->BSIM3leff
					    * model->BSIM3cox);
				      break;
			         case 2:
			         case 3:
			              vgs = *(ckt->CKTstates[0] + here->BSIM3vgs);
		                      vds = *(ckt->CKTstates[0] + here->BSIM3vds);
			              if (vds < 0.0)
			              {   vds = -vds;
				          vgs = vgs + vds;
			              }
                                      if (vgs >= here->BSIM3von + 0.1)
			              {   Ssi = StrongInversionNoiseEval(vgs,
					      vds, model, here, data->freq,
					      ckt->CKTtemp);
                                          noizDens[BSIM3FLNOIZ] *= Ssi;
			              }
                                      else 
			              {   pParam = here->pParam;
				          T10 = model->BSIM3oxideTrapDensityA
					      * 8.62e-5 * ckt->CKTtemp;
		                          T11 = pParam->BSIM3weff
					      * pParam->BSIM3leff
				              * pow(data->freq, model->BSIM3ef)
				              * 4.0e36;
		                          Swi = T10 / T11 * here->BSIM3cd
				              * here->BSIM3cd;
                                          Slimit = StrongInversionNoiseEval(
				               here->BSIM3von + 0.1, vds, model,
					       here, data->freq, ckt->CKTtemp);
				          T1 = Swi + Slimit;
				          if (T1 > 0.0)
                                              noizDens[BSIM3FLNOIZ] *= (Slimit
								    * Swi) / T1; 
				          else
                                              noizDens[BSIM3FLNOIZ] *= 0.0;
			              }
				      break;
			      }

		              lnNdens[BSIM3FLNOIZ] =
				     log(MAX(noizDens[BSIM3FLNOIZ], N_MINLOG));

		              noizDens[BSIM3TOTNOIZ] = noizDens[BSIM3RDNOIZ]
						     + noizDens[BSIM3RSNOIZ]
						     + noizDens[BSIM3IDNOIZ]
						     + noizDens[BSIM3FLNOIZ];
		              lnNdens[BSIM3TOTNOIZ] = 
				     log(MAX(noizDens[BSIM3TOTNOIZ], N_MINLOG));

		              *OnDens += noizDens[BSIM3TOTNOIZ];

		              if (data->delFreq == 0.0)
			      {   /* if we haven't done any previous 
				     integration, we need to initialize our
				     "history" variables.
				    */

			          for (i = 0; i < BSIM3NSRCS; i++)
				  {    here->BSIM3nVar[LNLSTDENS][i] =
					     lnNdens[i];
			          }

			          /* clear out our integration variables
				     if it's the first pass
				   */
			          if (data->freq ==
				      ((NOISEAN*) ckt->CKTcurJob)->NstartFreq)
				  {   for (i = 0; i < BSIM3NSRCS; i++)
				      {    here->BSIM3nVar[OUTNOIZ][i] = 0.0;
				           here->BSIM3nVar[INNOIZ][i] = 0.0;
			              }
			          }
		              }
			      else
			      {   /* data->delFreq != 0.0,
				     we have to integrate.
				   */
			          for (i = 0; i < BSIM3NSRCS; i++)
				  {    if (i != BSIM3TOTNOIZ)
				       {   tempOnoise = Nintegrate(noizDens[i],
						lnNdens[i],
				                here->BSIM3nVar[LNLSTDENS][i],
						data);
				           tempInoise = Nintegrate(noizDens[i]
						* data->GainSqInv, lnNdens[i]
						+ data->lnGainInv,
				                here->BSIM3nVar[LNLSTDENS][i]
						+ data->lnGainInv, data);
				           here->BSIM3nVar[LNLSTDENS][i] =
						lnNdens[i];
				           data->outNoiz += tempOnoise;
				           data->inNoise += tempInoise;
				           if (((NOISEAN*)
					       ckt->CKTcurJob)->NStpsSm != 0)
					   {   here->BSIM3nVar[OUTNOIZ][i]
						     += tempOnoise;
				               here->BSIM3nVar[OUTNOIZ][BSIM3TOTNOIZ]
						     += tempOnoise;
				               here->BSIM3nVar[INNOIZ][i]
						     += tempInoise;
				               here->BSIM3nVar[INNOIZ][BSIM3TOTNOIZ]
						     += tempInoise;
                                           }
			               }
			          }
		              }
		              if (data->prtSummary)
			      {   for (i = 0; i < BSIM3NSRCS; i++)
				  {    /* print a summary report */
			               data->outpVector[data->outNumber++]
					     = noizDens[i];
			          }
		              }
		              break;
		         case INT_NOIZ:
			      /* already calculated, just output */
		              if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
			      {   for (i = 0; i < BSIM3NSRCS; i++)
				  {    data->outpVector[data->outNumber++]
					     = here->BSIM3nVar[OUTNOIZ][i];
			               data->outpVector[data->outNumber++]
					     = here->BSIM3nVar[INNOIZ][i];
			          }
		              }
		              break;
		      }
		      break;
	         case N_CLOSE:
		      /* do nothing, the main calling routine will close */
		      return (OK);
		      break;   /* the plots */
	      }       /* switch (operation) */
	 }    /* for here */
    }    /* for model */

    return(OK);
}



