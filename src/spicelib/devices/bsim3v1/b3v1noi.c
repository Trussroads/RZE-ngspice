/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1995 Gary W. Ng and Min-Chie Jeng.
File:  b3v1noi.c
**********/

#include "ngspice.h"
#include <stdio.h>
#include <math.h>
#include "bsim3v1def.h"
#include "cktdefs.h"
#include "iferrmsg.h"
#include "noisedef.h"
#include "suffix.h"
#include "const.h"  /* jwan */

/*
 * BSIM3V1noise (mode, operation, firstModel, ckt, data, OnDens)
 *    This routine names and evaluates all of the noise sources
 *    associated with MOSFET's.  It starts with the model *firstModel and
 *    traverses all of its insts.  It then proceeds to any other models
 *    on the linked list.  The total output noise density generated by
 *    all of the MOSFET's is summed with the variable "OnDens".
 */

/*
 Channel thermal and flicker noises are calculated based on the value
 of model->BSIM3V1noiMod.
 If model->BSIM3V1noiMod = 1,
    Channel thermal noise = SPICE2 model
    Flicker noise         = SPICE2 model
 If model->BSIM3V1noiMod = 2,
    Channel thermal noise = BSIM3V1 model
    Flicker noise         = BSIM3V1 model
 If model->BSIM3V1noiMod = 3,
    Channel thermal noise = SPICE2 model
    Flicker noise         = BSIM3V1 model
 If model->BSIM3V1noiMod = 4,
    Channel thermal noise = BSIM3V1 model
    Flicker noise         = SPICE2 model
 */

extern void   NevalSrc();
extern double Nintegrate();

double
StrongInversionNoiseEval(vgs, vds, model, here, freq, temp)
double vgs, vds, freq, temp;
BSIM3V1model *model;
BSIM3V1instance *here;
{
struct bsim3v1SizeDependParam *pParam;
double cd, esat, DelClm, EffFreq, N0, Nl, Vgst;
double T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, Ssi;

    pParam = here->pParam;
    cd = fabs(here->BSIM3V1cd);
    if (vds > here->BSIM3V1vdsat)
    {   esat = 2.0 * pParam->BSIM3V1vsattemp / here->BSIM3V1ueff;
	T0 = ((((vds - here->BSIM3V1vdsat) / pParam->BSIM3V1litl) + model->BSIM3V1em)
	   / esat);
        DelClm = pParam->BSIM3V1litl * log (MAX(T0, N_MINLOG));
    }
    else 
        DelClm = 0.0;
    EffFreq = pow(freq, model->BSIM3V1ef);
    T1 = CHARGE * CHARGE * 8.62e-5 * cd * temp * here->BSIM3V1ueff;
    T2 = 1.0e8 * EffFreq * model->BSIM3V1cox
       * pParam->BSIM3V1leff * pParam->BSIM3V1leff;
    Vgst = vgs - here->BSIM3V1von;
    N0 = model->BSIM3V1cox * Vgst / CHARGE;
    if (N0 < 0.0)
	N0 = 0.0;
    Nl = model->BSIM3V1cox * (Vgst - MIN(vds, here->BSIM3V1vdsat)) / CHARGE;
    if (Nl < 0.0)
	Nl = 0.0;

    T3 = model->BSIM3V1oxideTrapDensityA
       * log(MAX(((N0 + 2.0e14) / (Nl + 2.0e14)), N_MINLOG));
    T4 = model->BSIM3V1oxideTrapDensityB * (N0 - Nl);
    T5 = model->BSIM3V1oxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

    T6 = 8.62e-5 * temp * cd * cd;
    T7 = 1.0e8 * EffFreq * pParam->BSIM3V1leff
       * pParam->BSIM3V1leff * pParam->BSIM3V1weff;
    T8 = model->BSIM3V1oxideTrapDensityA + model->BSIM3V1oxideTrapDensityB * Nl
       + model->BSIM3V1oxideTrapDensityC * Nl * Nl;
    T9 = (Nl + 2.0e14) * (Nl + 2.0e14);

    Ssi = T1 / T2 * (T3 + T4 + T5) + T6 / T7 * DelClm * T8 / T9;
    return Ssi;
}

int
BSIM3V1noise (mode, operation, inModel, ckt, data, OnDens)
int mode, operation;
GENmodel *inModel;
CKTcircuit *ckt;
register Ndata *data;
double *OnDens;
{
register BSIM3V1model *model = (BSIM3V1model *)inModel;
register BSIM3V1instance *here;
struct bsim3v1SizeDependParam *pParam;
char name[N_MXVLNTH];
double tempOnoise;
double tempInoise;
double noizDens[BSIM3V1NSRCS];
double lnNdens[BSIM3V1NSRCS];

double vgs, vds, Slimit;
double N0, Nl;
double T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13;
double n, ExpArg, Ssi, Swi;

int error, i;

    /* define the names of the noise sources */
    static char *BSIM3V1nNames[BSIM3V1NSRCS] =
    {   /* Note that we have to keep the order */
	".rd",              /* noise due to rd */
			    /* consistent with the index definitions */
	".rs",              /* noise due to rs */
			    /* in BSIM3V1defs.h */
	".id",              /* noise due to id */
	".1overf",          /* flicker (1/f) noise */
	""                  /* total transistor noise */
    };

    for (; model != NULL; model = model->BSIM3V1nextModel)
    {    for (here = model->BSIM3V1instances; here != NULL;
	      here = here->BSIM3V1nextInstance)
	 {    pParam = here->pParam;
	      switch (operation)
	      {  case N_OPEN:
		     /* see if we have to to produce a summary report */
		     /* if so, name all the noise generators */

		      if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
		      {   switch (mode)
			  {  case N_DENS:
			          for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    (void) sprintf(name, "onoise.%s%s",
					              here->BSIM3V1name,
						      BSIM3V1nNames[i]);
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
			          for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    (void) sprintf(name, "onoise_total.%s%s",
						      here->BSIM3V1name,
						      BSIM3V1nNames[i]);
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
						      here->BSIM3V1name,
						      BSIM3V1nNames[i]);
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
		              NevalSrc(&noizDens[BSIM3V1RDNOIZ],
				       &lnNdens[BSIM3V1RDNOIZ], ckt, THERMNOISE,
				       here->BSIM3V1dNodePrime, here->BSIM3V1dNode,
				       here->BSIM3V1drainConductance);

		              NevalSrc(&noizDens[BSIM3V1RSNOIZ],
				       &lnNdens[BSIM3V1RSNOIZ], ckt, THERMNOISE,
				       here->BSIM3V1sNodePrime, here->BSIM3V1sNode,
				       here->BSIM3V1sourceConductance);

                              switch( model->BSIM3V1noiMod )
			      {  case 1:
			         case 3:
			              NevalSrc(&noizDens[BSIM3V1IDNOIZ],
				               &lnNdens[BSIM3V1IDNOIZ], ckt, 
					       THERMNOISE, here->BSIM3V1dNodePrime,
				               here->BSIM3V1sNodePrime,
                                               (2.0 / 3.0 * fabs(here->BSIM3V1gm
				               + here->BSIM3V1gds
					       + here->BSIM3V1gmbs)));
				      break;
			         case 2:
			         case 4:
		                      NevalSrc(&noizDens[BSIM3V1IDNOIZ],
				               &lnNdens[BSIM3V1IDNOIZ], ckt,
					       THERMNOISE, here->BSIM3V1dNodePrime,
                                               here->BSIM3V1sNodePrime,
					       (here->BSIM3V1ueff
					       * fabs(here->BSIM3V1qinv
					       / (pParam->BSIM3V1leff
					       * pParam->BSIM3V1leff))));
				      break;
			      }
		              NevalSrc(&noizDens[BSIM3V1FLNOIZ], (double*) NULL,
				       ckt, N_GAIN, here->BSIM3V1dNodePrime,
				       here->BSIM3V1sNodePrime, (double) 0.0);

                              switch( model->BSIM3V1noiMod )
			      {  case 1:
			         case 4:
			              noizDens[BSIM3V1FLNOIZ] *= model->BSIM3V1kf
					    * exp(model->BSIM3V1af
					    * log(MAX(fabs(here->BSIM3V1cd),
					    N_MINLOG)))
					    / (pow(data->freq, model->BSIM3V1ef)
					    * pParam->BSIM3V1leff
				            * pParam->BSIM3V1leff
					    * model->BSIM3V1cox);
				      break;
			         case 2:
			         case 3:
			              vgs = *(ckt->CKTstates[0] + here->BSIM3V1vgs);
		                      vds = *(ckt->CKTstates[0] + here->BSIM3V1vds);
			              if (vds < 0.0)
			              {   vds = -vds;
				          vgs = vgs + vds;
			              }
                                      if (vgs >= here->BSIM3V1von + 0.1)
			              {   Ssi = StrongInversionNoiseEval(vgs,
					      vds, model, here, data->freq,
					      ckt->CKTtemp);
                                          noizDens[BSIM3V1FLNOIZ] *= Ssi;
			              }
                                      else 
			              {   pParam = here->pParam;
				          T10 = model->BSIM3V1oxideTrapDensityA
					      * 8.62e-5 * ckt->CKTtemp;
		                          T11 = pParam->BSIM3V1weff
					      * pParam->BSIM3V1leff
				              * pow(data->freq, model->BSIM3V1ef)
				              * 4.0e36;
		                          Swi = T10 / T11 * here->BSIM3V1cd
				              * here->BSIM3V1cd;
                                          Slimit = StrongInversionNoiseEval(
				               here->BSIM3V1von + 0.1, vds, model,
					       here, data->freq, ckt->CKTtemp);
				          T1 = Swi + Slimit;
				          if (T1 > 0.0)
                                              noizDens[BSIM3V1FLNOIZ] *= (Slimit
								    * Swi) / T1; 
				          else
                                              noizDens[BSIM3V1FLNOIZ] *= 0.0;
			              }
				      break;
			      }

		              lnNdens[BSIM3V1FLNOIZ] =
				     log(MAX(noizDens[BSIM3V1FLNOIZ], N_MINLOG));

		              noizDens[BSIM3V1TOTNOIZ] = noizDens[BSIM3V1RDNOIZ]
						     + noizDens[BSIM3V1RSNOIZ]
						     + noizDens[BSIM3V1IDNOIZ]
						     + noizDens[BSIM3V1FLNOIZ];
		              lnNdens[BSIM3V1TOTNOIZ] = 
				     log(MAX(noizDens[BSIM3V1TOTNOIZ], N_MINLOG));

		              *OnDens += noizDens[BSIM3V1TOTNOIZ];

		              if (data->delFreq == 0.0)
			      {   /* if we haven't done any previous 
				     integration, we need to initialize our
				     "history" variables.
				    */

			          for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    here->BSIM3V1nVar[LNLSTDENS][i] =
					     lnNdens[i];
			          }

			          /* clear out our integration variables
				     if it's the first pass
				   */
			          if (data->freq ==
				      ((NOISEAN*) ckt->CKTcurJob)->NstartFreq)
				  {   for (i = 0; i < BSIM3V1NSRCS; i++)
				      {    here->BSIM3V1nVar[OUTNOIZ][i] = 0.0;
				           here->BSIM3V1nVar[INNOIZ][i] = 0.0;
			              }
			          }
		              }
			      else
			      {   /* data->delFreq != 0.0,
				     we have to integrate.
				   */
			          for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    if (i != BSIM3V1TOTNOIZ)
				       {   tempOnoise = Nintegrate(noizDens[i],
						lnNdens[i],
				                here->BSIM3V1nVar[LNLSTDENS][i],
						data);
				           tempInoise = Nintegrate(noizDens[i]
						* data->GainSqInv, lnNdens[i]
						+ data->lnGainInv,
				                here->BSIM3V1nVar[LNLSTDENS][i]
						+ data->lnGainInv, data);
				           here->BSIM3V1nVar[LNLSTDENS][i] =
						lnNdens[i];
				           data->outNoiz += tempOnoise;
				           data->inNoise += tempInoise;
				           if (((NOISEAN*)
					       ckt->CKTcurJob)->NStpsSm != 0)
					   {   here->BSIM3V1nVar[OUTNOIZ][i]
						     += tempOnoise;
				               here->BSIM3V1nVar[OUTNOIZ][BSIM3V1TOTNOIZ]
						     += tempOnoise;
				               here->BSIM3V1nVar[INNOIZ][i]
						     += tempInoise;
				               here->BSIM3V1nVar[INNOIZ][BSIM3V1TOTNOIZ]
						     += tempInoise;
                                           }
			               }
			          }
		              }
		              if (data->prtSummary)
			      {   for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    /* print a summary report */
			               data->outpVector[data->outNumber++]
					     = noizDens[i];
			          }
		              }
		              break;
		         case INT_NOIZ:
			      /* already calculated, just output */
		              if (((NOISEAN*)ckt->CKTcurJob)->NStpsSm != 0)
			      {   for (i = 0; i < BSIM3V1NSRCS; i++)
				  {    data->outpVector[data->outNumber++]
					     = here->BSIM3V1nVar[OUTNOIZ][i];
			               data->outpVector[data->outNumber++]
					     = here->BSIM3V1nVar[INNOIZ][i];
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



