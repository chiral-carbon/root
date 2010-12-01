// @(#)root/tmva $Id$
// Author: Andreas Hoecker, Peter Speckmayer, Joerg Stelzer, Helge Voss, Kai Voss

/**********************************************************************************
 * Project: TMVA - a Root-integrated toolkit for multivariate data analysis       *
 * Package: TMVA                                                                  *
 * Class  : MethodBase                                                            *
 * Web    : http://tmva.sourceforge.net                                           *
 *                                                                                *
 * Description:                                                                   *
 *      Implementation (see header for description)                               *
 *                                                                                *
 * Authors (alphabetical):                                                        *
 *      Andreas Hoecker <Andreas.Hocker@cern.ch> - CERN, Switzerland              *
 *      Joerg Stelzer   <Joerg.Stelzer@cern.ch>  - CERN, Switzerland              *
 *      Peter Speckmayer  <Peter.Speckmayer@cern.ch>  - CERN, Switzerland         *
 *      Helge Voss      <Helge.Voss@cern.ch>     - MPI-K Heidelberg, Germany      *
 *      Kai Voss        <Kai.Voss@cern.ch>       - U. of Victoria, Canada         *
 *                                                                                *
 * Copyright (c) 2005:                                                            *
 *      CERN, Switzerland                                                         *
 *      U. of Victoria, Canada                                                    *
 *      MPI-K Heidelberg, Germany                                                 *
 *      LAPP, Annecy, France                                                      *
 *                                                                                *
 * Redistribution and use in source and binary forms, with or without             *
 * modification, are permitted according to the terms listed in LICENSE           *
 * (http://tmva.sourceforge.net/LICENSE)                                          *
 *                                                                                *
 **********************************************************************************/

//_______________________________________________________________________
/* Begin_Html
   Virtual base Class for all MVA method

   MethodBase hosts several specific evaluation methods.

   The kind of MVA that provides optimal performance in an analysis strongly
   depends on the particular application. The evaluation factory provides a
   number of numerical benchmark results to directly assess the performance
   of the MVA training on the independent test sample. These are:
   <ul>
   <li> The <i>signal efficiency</i> at three representative background efficiencies
   (which is 1 &minus; rejection).</li>
   <li> The <i>significance</I> of an MVA estimator, defined by the difference
   between the MVA mean values for signal and background, divided by the
   quadratic sum of their root mean squares.</li>
   <li> The <i>separation</i> of an MVA <i>x</i>, defined by the integral
   &frac12;&int;(S(x) &minus; B(x))<sup>2</sup>/(S(x) + B(x))dx, where
   S(x) and B(x) are the signal and background distributions, respectively.
   The separation is zero for identical signal and background MVA shapes,
   and it is one for disjunctive shapes.
   <li> <a name="mu_transform">
   The average, &int;x &mu;(S(x))dx, of the signal &mu;-transform.
   The &mu;-transform of an MVA denotes the transformation that yields
   a uniform background distribution. In this way, the signal distributions
   S(x) can be directly compared among the various MVAs. The stronger S(x)
   peaks towards one, the better is the discrimination of the MVA. The
   &mu;-transform is
   <a href=http://tel.ccsd.cnrs.fr/documents/archives0/00/00/29/91/index_fr.html>documented here</a>.
   </ul>
   The MVA standard output also prints the linear correlation coefficients between
   signal and background, which can be useful to eliminate variables that exhibit too
   strong correlations.

   End_Html */
//_______________________________________________________________________

#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <limits>

#include "TROOT.h"
#include "TSystem.h"
#include "TObjString.h"
#include "TQObject.h"
#include "TSpline.h"
#include "TMatrix.h"
#include "TMath.h"
#include "TFile.h"
#include "TKey.h"
#include "TGraph.h"
#include "Riostream.h"
#include "TXMLEngine.h"

#include "TMVA/MsgLogger.h"
#include "TMVA/MethodBase.h"
#include "TMVA/Config.h"
#include "TMVA/Timer.h"
#include "TMVA/RootFinder.h"
#include "TMVA/PDF.h"
#include "TMVA/VariableIdentityTransform.h"
#include "TMVA/VariableDecorrTransform.h"
#include "TMVA/VariablePCATransform.h"
#include "TMVA/VariableGaussTransform.h"
#include "TMVA/VariableNormalizeTransform.h"
#include "TMVA/Version.h"
#include "TMVA/TSpline1.h"
#include "TMVA/Ranking.h"
#include "TMVA/Factory.h"
#include "TMVA/Tools.h"
#include "TMVA/ResultsClassification.h"
#include "TMVA/ResultsRegression.h"
#include "TMVA/ResultsMulticlass.h"

ClassImp(TMVA::MethodBase)

using std::endl;

const Int_t    MethodBase_MaxIterations_ = 200;
const Bool_t   Use_Splines_for_Eff_      = kTRUE;

const Int_t    NBIN_HIST_PLOT = 100;
const Int_t    NBIN_HIST_HIGH = 10000;

#ifdef _WIN32
/* Disable warning C4355: 'this' : used in base member initializer list */
#pragma warning ( disable : 4355 )
#endif

//_______________________________________________________________________
TMVA::MethodBase::MethodBase( const TString& jobName,
                              Types::EMVA methodType,
                              const TString& methodTitle,
                              DataSetInfo& dsi,
                              const TString& theOption,
                              TDirectory* theBaseDir) :
   IMethod(),
   Configurable               ( theOption ),
   fTmpEvent                  ( 0 ),
   fAnalysisType              ( Types::kNoAnalysisType ),
   fRegressionReturnVal       ( 0 ),
   fMulticlassReturnVal       ( 0 ),
   fDisableWriting            ( kFALSE ),
   fDataSetInfo               ( dsi ),
   fSignalReferenceCut        ( 0.5 ),
   fVariableTransformType     ( Types::kSignal ),
   fJobName                   ( jobName ),
   fMethodName                ( methodTitle ),
   fMethodType                ( methodType ),
   fTestvar                   ( "" ),
   fTMVATrainingVersion       ( TMVA_VERSION_CODE ),
   fROOTTrainingVersion       ( ROOT_VERSION_CODE ),
   fConstructedFromWeightFile ( kFALSE ),
   fBaseDir                   ( 0 ),
   fMethodBaseDir             ( theBaseDir ),
   fWeightFile                ( "" ),
   fDefaultPDF                ( 0 ),
   fMVAPdfS                   ( 0 ),
   fMVAPdfB                   ( 0 ),
   fSplS                      ( 0 ),
   fSplB                      ( 0 ),
   fSpleffBvsS                ( 0 ),
   fSplTrainS                 ( 0 ),
   fSplTrainB                 ( 0 ),
   fSplTrainEffBvsS           ( 0 ),
   fVarTransformString        ( "None" ),
   fTransformation            ( dsi, methodTitle ),
   fVerbose                   ( kFALSE ),
   fVerbosityLevelString      ( "Default" ),
   fHelp                      ( kFALSE ),
   fHasMVAPdfs                ( kFALSE ),
   fIgnoreNegWeightsInTraining( kFALSE ),
   fSignalClass               ( 0 ),
   fBackgroundClass           ( 0 ),
   fSplRefS                   ( 0 ),
   fSplRefB                   ( 0 ),
   fSplTrainRefS              ( 0 ),
   fSplTrainRefB              ( 0 ),
   fSetupCompleted            (kFALSE)
{
   // standard constructur
   SetTestvarName();

   // default extension for weight files
   SetWeightFileDir( gConfig().GetIONames().fWeightFileDir );
   gSystem->MakeDirectory( GetWeightFileDir() );
}

//_______________________________________________________________________
TMVA::MethodBase::MethodBase( Types::EMVA methodType,
                              DataSetInfo& dsi,
                              const TString& weightFile,
                              TDirectory* theBaseDir ) :
   IMethod(),
   Configurable(""),
   fTmpEvent                  ( 0 ),
   fRegressionReturnVal       ( 0 ),
   fMulticlassReturnVal       ( 0 ),
   fDataSetInfo               ( dsi ),
   fSignalReferenceCut        ( 0.5 ),
   fVariableTransformType     ( Types::kSignal ),
   fJobName                   ( "" ),
   fMethodName                ( "MethodBase"  ),
   fMethodType                ( methodType ),
   fTestvar                   ( "" ),
   fTMVATrainingVersion       ( 0 ),
   fROOTTrainingVersion       ( 0 ),
   fConstructedFromWeightFile ( kTRUE ),
   fBaseDir                   ( theBaseDir ),
   fMethodBaseDir             ( 0 ),
   fWeightFile                ( weightFile ),
   fDefaultPDF                ( 0 ),
   fMVAPdfS                   ( 0 ),
   fMVAPdfB                   ( 0 ),
   fSplS                      ( 0 ),
   fSplB                      ( 0 ),
   fSpleffBvsS                ( 0 ),
   fSplTrainS                 ( 0 ),
   fSplTrainB                 ( 0 ),
   fSplTrainEffBvsS           ( 0 ),
   fVarTransformString        ( "None" ),
   fTransformation            ( dsi, "" ),
   fVerbose                   ( kFALSE ),
   fVerbosityLevelString      ( "Default" ),
   fHelp                      ( kFALSE ),
   fHasMVAPdfs                ( kFALSE ),
   fIgnoreNegWeightsInTraining( kFALSE ),
   fSignalClass               ( 0 ),
   fBackgroundClass           ( 0 ),
   fSplRefS                   ( 0 ),
   fSplRefB                   ( 0 ),
   fSplTrainRefS              ( 0 ),
   fSplTrainRefB              ( 0 ),
   fSetupCompleted            (kFALSE)
{
   // constructor used for Testing + Application of the MVA,
   // only (no training), using given WeightFiles
}

//_______________________________________________________________________
TMVA::MethodBase::~MethodBase( void )
{
   // destructor
   if (!fSetupCompleted) Log() << kFATAL << "Calling destructor of method which got never setup" << Endl;

   // destructor
   if (fInputVars != 0)  { fInputVars->clear(); delete fInputVars; }
   if (fRanking   != 0)  delete fRanking;

   // PDFs
   if (fDefaultPDF!= 0)  { delete fDefaultPDF; fDefaultPDF = 0; }
   if (fMVAPdfS   != 0)  { delete fMVAPdfS; fMVAPdfS = 0; }
   if (fMVAPdfB   != 0)  { delete fMVAPdfB; fMVAPdfB = 0; }

   // Splines
   if (fSplS)            { delete fSplS; fSplS = 0; }
   if (fSplB)            { delete fSplB; fSplB = 0; }
   if (fSpleffBvsS)      { delete fSpleffBvsS; fSpleffBvsS = 0; }
   if (fSplRefS)         { delete fSplRefS; fSplRefS = 0; }
   if (fSplRefB)         { delete fSplRefB; fSplRefB = 0; }
   if (fSplTrainRefS)    { delete fSplTrainRefS; fSplTrainRefS = 0; }
   if (fSplTrainRefB)    { delete fSplTrainRefB; fSplTrainRefB = 0; }
   if (fSplTrainEffBvsS) { delete fSplTrainEffBvsS; fSplTrainEffBvsS = 0; }

   for (Int_t i = 0; i < 2; i++ ) {
      if (fEventCollections.at(i)) {
         for (std::vector<Event*>::const_iterator it = fEventCollections.at(i)->begin();
              it != fEventCollections.at(i)->end(); it++) {
            delete (*it);
         }
         delete fEventCollections.at(i);
         fEventCollections.at(i) = 0;
      }
   }

   if (fRegressionReturnVal) delete fRegressionReturnVal;
   if (fMulticlassReturnVal) delete fMulticlassReturnVal;
}

//_______________________________________________________________________
void TMVA::MethodBase::SetupMethod()
{
   // setup of methods

   if (fSetupCompleted) Log() << kFATAL << "Calling SetupMethod for the second time" << Endl;
   InitBase();
   DeclareBaseOptions();
   Init();
   DeclareOptions();
   fSetupCompleted = kTRUE;
}

//_______________________________________________________________________
void TMVA::MethodBase::ProcessSetup()
{
   // process all options
   // the "CheckForUnusedOptions" is done in an independent call, since it may be overridden by derived class
   // (sometimes, eg, fitters are used which can only be implemented during training phase)
   ProcessBaseOptions();
   ProcessOptions();
}

//_______________________________________________________________________
void TMVA::MethodBase::CheckSetup()
{
   // check may be overridden by derived class
   // (sometimes, eg, fitters are used which can only be implemented during training phase)
   CheckForUnusedOptions();
}

//_______________________________________________________________________
void TMVA::MethodBase::InitBase()
{
   // default initialization called by all constructors
   SetConfigDescription( "Configuration options for classifier architecture and tuning" );

   fNbins              = gConfig().fVariablePlotting.fNbinsXOfROCCurve;
   fNbinsH             = NBIN_HIST_HIGH;

   fSplTrainS          = 0;
   fSplTrainB          = 0;
   fSplTrainEffBvsS    = 0;
   fMeanS              = -1;
   fMeanB              = -1;
   fRmsS               = -1;
   fRmsB               = -1;
   fXmin               = DBL_MAX;
   fXmax               = -DBL_MAX;
   fTxtWeightsOnly     = kTRUE;
   fSplRefS            = 0;
   fSplRefB            = 0;

   fTrainTime          = -1.;
   fTestTime           = -1.;

   fRanking            = 0;

   // temporary until the move to DataSet is complete
   fInputVars = new std::vector<TString>;
   for (UInt_t ivar=0; ivar<GetNvar(); ivar++) {
      fInputVars->push_back(DataInfo().GetVariableInfo(ivar).GetLabel());
   }
   fRegressionReturnVal = 0;
   fMulticlassReturnVal = 0;

   fEventCollections.resize( 2 );
   fEventCollections.at(0) = 0;
   fEventCollections.at(1) = 0;

   // define "this" pointer
   ResetThisBase();

   // retrieve signal and background class index
   if (DataInfo().GetClassInfo("Signal") != 0) {
      fSignalClass = DataInfo().GetClassInfo("Signal")->GetNumber();
   }
   if (DataInfo().GetClassInfo("Background") != 0) {
      fBackgroundClass = DataInfo().GetClassInfo("Background")->GetNumber();
   }

   SetConfigDescription( "Configuration options for MVA method" );
   SetConfigName( TString("Method") + GetMethodTypeName() );
}

//_______________________________________________________________________
void TMVA::MethodBase::DeclareBaseOptions()
{
   // define the options (their key words) that can be set in the option string
   // here the options valid for ALL MVA methods are declared.
   // know options: VariableTransform=None,Decorrelated,PCA  to use transformed variables
   //                                                        instead of the original ones
   //               VariableTransformType=Signal,Background  which decorrelation matrix to use
   //                                                        in the method. Only the Likelihood
   //                                                        Method can make proper use of independent
   //                                                        transformations of signal and background
   //               fNbinsMVAPdf   = 50 Number of bins used to create a PDF of MVA
   //               fNsmoothMVAPdf =  2 Number of times a histogram is smoothed before creating the PDF
   //               fHasMVAPdfs         create PDFs for the MVA outputs
   //               V                   for Verbose output (!V) for non verbos
   //               H                   for Help message

   DeclareOptionRef( fVerbose, "V", "Verbose output (short form of \"VerbosityLevel\" below - overrides the latter one)" );

   DeclareOptionRef( fVerbosityLevelString="Default", "VerbosityLevel", "Verbosity level" );
   AddPreDefVal( TString("Default") ); // uses default defined in MsgLogger header
   AddPreDefVal( TString("Debug")   );
   AddPreDefVal( TString("Verbose") );
   AddPreDefVal( TString("Info")    );
   AddPreDefVal( TString("Warning") );
   AddPreDefVal( TString("Error")   );
   AddPreDefVal( TString("Fatal")   );

   // If True (default): write all training results (weights) as text files only;
   // if False: write also in ROOT format (not available for all methods - will abort if not
   fTxtWeightsOnly = kTRUE;  // OBSOLETE !!!
   fNormalise      = kFALSE; // OBSOLETE !!!

   DeclareOptionRef( fVarTransformString, "VarTransform", "List of variable transformations performed before training, e.g., \"D_Background,P_Signal,G,N_AllClasses\" for: \"Decorrelation, PCA-transformation, Gaussianisation, Normalisation, each for the given class of events ('AllClasses' denotes all events of all classes, if no class indication is given, 'All' is assumed)\"" );

   DeclareOptionRef( fHelp, "H", "Print method-specific help message" );

   DeclareOptionRef( fHasMVAPdfs, "CreateMVAPdfs", "Create PDFs for classifier outputs (signal and background)" );

   DeclareOptionRef( fIgnoreNegWeightsInTraining, "IgnoreNegWeightsInTraining",
                     "Events with negative weights are ignored in the training (but are included for testing and performance evaluation)" );
}

//_______________________________________________________________________
void TMVA::MethodBase::ProcessBaseOptions()
{
   // the option string is decoded, for availabel options see "DeclareOptions"

   if (HasMVAPdfs()) {
      // setting the default bin num... maybe should be static ? ==> Please no static (JS)
      // You can't use the logger in the constructor!!! Log() << kINFO << "Create PDFs" << Endl;
      // reading every PDF's definition and passing the option string to the next one to be read and marked
      fDefaultPDF = new PDF( TString(GetName())+"_PDF", GetOptions(), "MVAPdf" );
      fDefaultPDF->DeclareOptions();
      fDefaultPDF->ParseOptions();
      fDefaultPDF->ProcessOptions();
      fMVAPdfB = new PDF( TString(GetName())+"_PDFBkg", fDefaultPDF->GetOptions(), "MVAPdfBkg", fDefaultPDF );
      fMVAPdfB->DeclareOptions();
      fMVAPdfB->ParseOptions();
      fMVAPdfB->ProcessOptions();
      fMVAPdfS = new PDF( TString(GetName())+"_PDFSig", fMVAPdfB->GetOptions(),    "MVAPdfSig", fDefaultPDF );
      fMVAPdfS->DeclareOptions();
      fMVAPdfS->ParseOptions();
      fMVAPdfS->ProcessOptions();

      // the final marked option string is written back to the original methodbase
      SetOptions( fMVAPdfS->GetOptions() );
   }

   CreateVariableTransforms( fVarTransformString );

   if (!HasMVAPdfs()) {
      if (fDefaultPDF!= 0) { delete fDefaultPDF; fDefaultPDF = 0; }
      if (fMVAPdfS   != 0) { delete fMVAPdfS; fMVAPdfS = 0; }
      if (fMVAPdfB   != 0) { delete fMVAPdfB; fMVAPdfB = 0; }
   }

   if (fVerbose) { // overwrites other settings
      fVerbosityLevelString = TString("Verbose");
      Log().SetMinType( kVERBOSE );
   }
   else if (fVerbosityLevelString == "Debug"   ) Log().SetMinType( kDEBUG );
   else if (fVerbosityLevelString == "Verbose" ) Log().SetMinType( kVERBOSE );
   else if (fVerbosityLevelString == "Info"    ) Log().SetMinType( kINFO );
   else if (fVerbosityLevelString == "Warning" ) Log().SetMinType( kWARNING );
   else if (fVerbosityLevelString == "Error"   ) Log().SetMinType( kERROR );
   else if (fVerbosityLevelString == "Fatal"   ) Log().SetMinType( kFATAL );
   else if (fVerbosityLevelString != "Default" ) {
      Log() << kFATAL << "<ProcessOptions> Verbosity level type '"
            << fVerbosityLevelString << "' unknown." << Endl;
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::CreateVariableTransforms(const TString& trafoDefinition )
{
   if (trafoDefinition != "None") {
      TList* trList = gTools().ParseFormatLine( trafoDefinition, "," );
      TListIter trIt(trList);
      while (TObjString* os = (TObjString*)trIt()) {
         Int_t idxCls = -1;

         TList* trClsList = gTools().ParseFormatLine( os->GetString(), "_" ); // split entry to get trf-name and class-name
         TListIter trClsIt(trClsList);
         const TString& trName = ((TObjString*)trClsList->At(0))->GetString();

         if (trClsList->GetEntries() > 1) {
            TString trCls = "AllClasses";
            ClassInfo *ci = NULL;
            trCls  = ((TObjString*)trClsList->At(1))->GetString();
            if (trCls != "AllClasses") {
               ci = DataInfo().GetClassInfo( trCls );
               if (ci == NULL)
                  Log() << kFATAL << "Class " << trCls << " not known for variable transformation "
                        << trName << ", please check." << Endl;
               else
                  idxCls = ci->GetNumber();
            }
         }

         if      (trName == "D" || trName == "Deco" || trName == "Decorrelate")
            GetTransformationHandler().AddTransformation( new VariableDecorrTransform   ( DataInfo()) , idxCls );
         else if (trName == "P" || trName == "PCA")
            GetTransformationHandler().AddTransformation( new VariablePCATransform      ( DataInfo()), idxCls );
         else if (trName == "U" || trName == "Uniform")
            GetTransformationHandler().AddTransformation( new VariableGaussTransform    ( DataInfo(),"Uniform"), idxCls );
         else if (trName == "G" || trName == "Gauss")
            GetTransformationHandler().AddTransformation( new VariableGaussTransform    ( DataInfo()), idxCls );
         else if (trName == "N" || trName == "Norm" || trName == "Normalise" || trName == "Normalize")
            GetTransformationHandler().AddTransformation( new VariableNormalizeTransform( DataInfo()), idxCls );
         else
            Log() << kFATAL << "<ProcessOptions> Variable transform '"
                  << trName << "' unknown." << Endl;
         ClassInfo* clsInfo = DataInfo().GetClassInfo(idxCls);
         if( clsInfo )
            Log() << kINFO << " create Transformation " << trName << " with reference class " <<clsInfo->GetName() << "=("<< idxCls <<")"<<Endl;
         else
            Log() << kINFO << " create Transformation " << trName << " with events of all classes." << Endl;

      }
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::DeclareCompatibilityOptions()
{
   DeclareOptionRef( fNormalise=kFALSE, "Normalise", "Normalise input variables" ); // don't change the default !!!
   DeclareOptionRef( fUseDecorr=kFALSE, "D", "Use-decorrelated-variables flag" );
   DeclareOptionRef( fVariableTransformTypeString="Signal", "VarTransformType",
                     "Use signal or background events to derive for variable transformation (the transformation is applied on both types of, course)" );
   AddPreDefVal( TString("Signal") );
   AddPreDefVal( TString("Background") );
   DeclareOptionRef( fTxtWeightsOnly=kTRUE, "TxtWeightFilesOnly", "If True: write all training results (weights) as text files (False: some are written in ROOT format)" );
   DeclareOptionRef( fVerbosityLevelString="Default", "VerboseLevel", "Verbosity level" );
   AddPreDefVal( TString("Default") ); // uses default defined in MsgLogger header
   AddPreDefVal( TString("Debug")   );
   AddPreDefVal( TString("Verbose") );
   AddPreDefVal( TString("Info")    );
   AddPreDefVal( TString("Warning") );
   AddPreDefVal( TString("Error")   );
   AddPreDefVal( TString("Fatal")   );
   DeclareOptionRef( fNbinsMVAPdf   = 60, "NbinsMVAPdf",   "Number of bins used for the PDFs of classifier outputs" );
   DeclareOptionRef( fNsmoothMVAPdf = 2,  "NsmoothMVAPdf", "Number of smoothing iterations for classifier PDFs" );
}


//_______________________________________________________________________
std::map<TString,Double_t>  TMVA::MethodBase::OptimizeTuningParameters(TString /* fomType */ , TString /* fitType */)
{
   // call the Optimzier with the set of paremeters and ranges that
   // are meant to be tuned.

   // this is just a dummy...  needs to be implemented for each method
   // individually (as long as we don't have it automatized via the
   // configuraion string

   Log() << kWARNING << "Parameter optimization is not yet implemented for method " 
         << GetName() << Endl; 
   Log() << kWARNING << "Currently we need to set hardcoded which parameter is tuned in which ranges"<<Endl;

   std::map<TString,Double_t> tunedParameters;
   tunedParameters.size(); // just to get rid of "unused" warning
   return tunedParameters;

}

//_______________________________________________________________________
void TMVA::MethodBase::SetTuneParameters(std::map<TString,Double_t> /* tuneParameters */)
{
   // set the tuning parameters accoding to the argument
   // This is just a dummy .. have a look at the MethodBDT how you could 
   // perhaps implment the same thing for the other Classifiers..
}

//_______________________________________________________________________
void TMVA::MethodBase::TrainMethod()
{
   Data()->SetCurrentType(Types::kTraining);

   // train the MVA method
   if (Help()) PrintHelpMessage();

   // all histograms should be created in the method's subdirectory
   BaseDir()->cd();

   GetTransformationHandler().CalcTransformations(Data()->GetEventCollection());

   // call training of derived MVA
   Log() << kINFO << "Begin training" << Endl;
   Long64_t nEvents = Data()->GetNEvents();
   Timer traintimer( nEvents, GetName(), kTRUE );
   Train();
   Log() << kINFO << "End of training                                              " << Endl;
   SetTrainTime(traintimer.ElapsedSeconds());
   Log() << kINFO << "Elapsed time for training with " << nEvents <<  " events: "
         << traintimer.GetElapsedTime() << "         " << Endl;

   Log() << kINFO << "Create MVA output for ";

   // create PDFs for the signal and background MVA distributions (if required)
   if (DoMulticlass()){
      Log() << "Multiclass classification on training sample" << Endl;
      AddMulticlassOutput(Types::kTraining);
   }
   else if (!DoRegression()) {

      Log() << "classification on training sample" << Endl;
      AddClassifierOutput(Types::kTraining);
      if (HasMVAPdfs()) {
         CreateMVAPdfs();
         AddClassifierOutputProb(Types::kTraining);
      }
      
   } else {
      
      Log() << "regression on training sample" << Endl;
      AddRegressionOutput( Types::kTraining );

      if (HasMVAPdfs() ) {
         Log() << "Create PDFs" << Endl;
         CreateMVAPdfs();
      }
   }

   // write the current MVA state into stream
   // produced are one text file and one ROOT file
   if( !fDisableWriting ) WriteStateToFile();

   // produce standalone make class (presently only supported for classification)
   if ((!DoRegression()) && (!fDisableWriting)) MakeClass();

   // write additional monitoring histograms to main target file (not the weight file)
   // again, make sure the histograms go into the method's subdirectory
   BaseDir()->cd();
   WriteMonitoringHistosToFile();
}

//_______________________________________________________________________
void TMVA::MethodBase::GetRegressionDeviation(UInt_t tgtNum, Types::ETreeType type, Double_t& stddev, Double_t& stddev90Percent ) const 
{
   if (!DoRegression()) Log() << kFATAL << "Trying to use GetRegressionDeviation() with a classification job" << Endl;
   Log() << kINFO << "Create results for " << (type==Types::kTraining?"training":"testing") << Endl;
   ResultsRegression* regRes = (ResultsRegression*)Data()->GetResults(GetMethodName(), Types::kTesting, Types::kRegression);
   bool truncate = false;
   TH1F* h1 = regRes->QuadraticDeviation( tgtNum , truncate, 1.);
   stddev = sqrt(h1->GetMean());
   truncate = true;
   Double_t yq[1], xq[]={0.9};
   h1->GetQuantiles(1,yq,xq);
   TH1F* h2 = regRes->QuadraticDeviation( tgtNum , truncate, yq[0]);
   stddev90Percent = sqrt(h2->GetMean());
   delete h1;
   delete h2;
}

//_______________________________________________________________________
void TMVA::MethodBase::AddRegressionOutput(Types::ETreeType type)
{
   // prepare tree branch with the method's discriminating variable

   Data()->SetCurrentType(type);

   Log() << kINFO << "Create results for " << (type==Types::kTraining?"training":"testing") << Endl;

   ResultsRegression* regRes = (ResultsRegression*)Data()->GetResults(GetMethodName(), type, Types::kRegression);

   Long64_t nEvents = Data()->GetNEvents();

   // use timer
   Timer timer( nEvents, GetName(), kTRUE );

   Log() << kINFO << "Evaluation of " << GetMethodName() << " on "
         << (type==Types::kTraining?"training":"testing") << " sample" << Endl;

   regRes->Resize( nEvents );
   for (Int_t ievt=0; ievt<nEvents; ievt++) {
      Data()->SetCurrentEvent(ievt);
      std::vector< Float_t > vals = GetRegressionValues();
      regRes->SetValue( vals, ievt );
      timer.DrawProgressBar( ievt );
   }

   Log() << kINFO << "Elapsed time for evaluation of " << nEvents <<  " events: "
         << timer.GetElapsedTime() << "       " << Endl;

   // store time used for testing
   if (type==Types::kTesting)
      SetTestTime(timer.ElapsedSeconds());

   TString histNamePrefix(GetTestvarName());
   histNamePrefix += (type==Types::kTraining?"train":"test");
   regRes->CreateDeviationHistograms( histNamePrefix );
}

//_______________________________________________________________________
void TMVA::MethodBase::AddMulticlassOutput(Types::ETreeType type)
{
   // prepare tree branch with the method's discriminating variable

   Data()->SetCurrentType(type);

   Log() << kINFO << "Create results for " << (type==Types::kTraining?"training":"testing") << Endl;

   ResultsMulticlass* resMulticlass = dynamic_cast<ResultsMulticlass*>(Data()->GetResults(GetMethodName(), type, Types::kMulticlass));
   if (!resMulticlass) Log() << kFATAL<< "unable to create pointer in AddMulticlassOutput, exiting."<<Endl;

   Long64_t nEvents = Data()->GetNEvents();

   // use timer
   Timer timer( nEvents, GetName(), kTRUE );

   Log() << kINFO << "Multiclass evaluation of " << GetMethodName() << " on "
         << (type==Types::kTraining?"training":"testing") << " sample" << Endl;

   resMulticlass->Resize( nEvents );
   for (Int_t ievt=0; ievt<nEvents; ievt++) {
      Data()->SetCurrentEvent(ievt);
      std::vector< Float_t > vals = GetMulticlassValues();
      resMulticlass->SetValue( vals, ievt );
      timer.DrawProgressBar( ievt );
   }

   Log() << kINFO << "Elapsed time for evaluation of " << nEvents <<  " events: "
         << timer.GetElapsedTime() << "       " << Endl;

   // store time used for testing
   if (type==Types::kTesting)
      SetTestTime(timer.ElapsedSeconds());

   TString histNamePrefix(GetTestvarName());
   histNamePrefix += (type==Types::kTraining?"_Train":"_Test");
   resMulticlass->CreateMulticlassHistos( histNamePrefix, fNbins, fNbinsH );
}



//_______________________________________________________________________
void TMVA::MethodBase::NoErrorCalc(Double_t* const err, Double_t* const errUpper) {
   if(err) *err=-1;
   if(errUpper) *errUpper=-1;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetMvaValue( const Event* const ev, Double_t* err, Double_t* errUpper ) {
   fTmpEvent = ev;
   Double_t val = GetMvaValue(err, errUpper);
   fTmpEvent = 0;
   return val;
}

//_______________________________________________________________________
void TMVA::MethodBase::AddClassifierOutput( Types::ETreeType type )
{
   // prepare tree branch with the method's discriminating variable

   Data()->SetCurrentType(type);

   ResultsClassification* clRes =
      (ResultsClassification*)Data()->GetResults(GetMethodName(), type, Types::kClassification );

   Long64_t nEvents = Data()->GetNEvents();

   // use timer
   Timer timer( nEvents, GetName(), kTRUE );

   Log() << kINFO << "Evaluation of " << GetMethodName() << " on "
         << (type==Types::kTraining?"training":"testing") << " sample (" << nEvents << " events)" << Endl;

   clRes->Resize( nEvents );
   for (Int_t ievt=0; ievt<nEvents; ievt++) {

      SetCurrentEvent(ievt);
      clRes->SetValue( GetMvaValue(), ievt );

      // print progress
      Int_t modulo = Int_t(nEvents/100);
      if( modulo <= 0 ) modulo = 1;
      if (ievt%modulo == 0) timer.DrawProgressBar( ievt );
   }

   Log() << kINFO << "Elapsed time for evaluation of " << nEvents <<  " events: "
         << timer.GetElapsedTime() << "       " << Endl;

   // store time used for testing
   if (type==Types::kTesting)
      SetTestTime(timer.ElapsedSeconds());

}

//_______________________________________________________________________
void TMVA::MethodBase::AddClassifierOutputProb( Types::ETreeType type )
{
   // prepare tree branch with the method's discriminating variable

   Data()->SetCurrentType(type);

   ResultsClassification* mvaProb = 
      (ResultsClassification*)Data()->GetResults(TString("prob_")+GetMethodName(), type, Types::kClassification );

   Long64_t nEvents = Data()->GetNEvents();

   // use timer
   Timer timer( nEvents, GetName(), kTRUE );

   Log() << kINFO << "Evaluation of " << GetMethodName() << " on "
         << (type==Types::kTraining?"training":"testing") << " sample" << Endl;

   mvaProb->Resize( nEvents );
   for (Int_t ievt=0; ievt<nEvents; ievt++) {

      Data()->SetCurrentEvent(ievt);
      Float_t proba = ((Float_t)GetProba( GetMvaValue(), 0.5 ));
      if (proba < 0) break;
      mvaProb->SetValue( proba, ievt );

      // print progress
      Int_t modulo = Int_t(nEvents/100);
      if( modulo <= 0 ) modulo = 1;
      if (ievt%modulo == 0) timer.DrawProgressBar( ievt );
   }

   Log() << kINFO << "Elapsed time for evaluation of " << nEvents <<  " events: "
         << timer.GetElapsedTime() << "       " << Endl;
}

//_______________________________________________________________________
void TMVA::MethodBase::TestRegression( Double_t& bias, Double_t& biasT, 
                                       Double_t& dev,  Double_t& devT, 
                                       Double_t& rms,  Double_t& rmsT, 
                                       Double_t& mInf, Double_t& mInfT,
                                       Double_t& corr, 
                                       Types::ETreeType type )
{
   // calculate <sum-of-deviation-squared> of regression output versus "true" value from test sample 
   //                    
   //   bias = average deviation
   //   dev  = average absolute deviation
   //   rms  = rms of deviation
   //

   Types::ETreeType savedType = Data()->GetCurrentType();
   Data()->SetCurrentType(type);

   bias = 0; biasT = 0; dev = 0; devT = 0; rms = 0; rmsT = 0;
   Double_t sumw = 0;
   Double_t m1 = 0, m2 = 0, s1 = 0, s2 = 0, s12 = 0; // for correlation
   const Int_t nevt = GetNEvents();
   Float_t* rV = new Float_t[nevt];
   Float_t* tV = new Float_t[nevt];
   Float_t* wV = new Float_t[nevt];
   Float_t  xmin = 1e30, xmax = -1e30;
   for (Long64_t ievt=0; ievt<nevt; ievt++) {
      
      const Event* ev = Data()->GetEvent(ievt); // NOTE: need untransformed event here !
      Float_t t = ev->GetTarget(0);
      Float_t w = ev->GetWeight();
      Float_t r = GetRegressionValues()[0];
      Float_t d = (r-t);

      // find min/max
      xmin = TMath::Min(xmin, TMath::Min(t, r));
      xmax = TMath::Max(xmax, TMath::Max(t, r));

      // store for truncated RMS computation
      rV[ievt] = r;
      tV[ievt] = t;
      wV[ievt] = w;
      
      // compute deviation-squared
      sumw += w;
      bias += w * d;
      dev  += w * TMath::Abs(d);
      rms  += w * d * d;

      // compute correlation between target and regression estimate
      m1  += t*w; s1 += t*t*w;
      m2  += r*w; s2 += r*r*w;
      s12 += t*r;
   }

   // standard quantities
   bias /= sumw;
   dev  /= sumw;
   rms  /= sumw;
   rms  = TMath::Sqrt(rms - bias*bias);

   // correlation
   m1   /= sumw; 
   m2   /= sumw; 
   corr  = s12/sumw - m1*m2;
   corr /= TMath::Sqrt( (s1/sumw - m1*m1) * (s2/sumw - m2*m2) );

   // create histogram required for computeation of mutual information
   TH2F* hist  = new TH2F( "hist",  "hist",  150, xmin, xmax, 100, xmin, xmax );
   TH2F* histT = new TH2F( "histT", "histT", 150, xmin, xmax, 100, xmin, xmax );

   // compute truncated RMS and fill histogram
   Double_t devMax = bias + 2*rms;
   Double_t devMin = bias - 2*rms;
   sumw = 0;
   int ic=0;
   for (Long64_t ievt=0; ievt<nevt; ievt++) {
      Float_t d = (rV[ievt] - tV[ievt]);
      hist->Fill( rV[ievt], tV[ievt], wV[ievt] );
      if (d >= devMin && d <= devMax) {
         sumw  += wV[ievt];
         biasT += wV[ievt] * d;
         devT  += wV[ievt] * TMath::Abs(d);
         rmsT  += wV[ievt] * d * d;       
         histT->Fill( rV[ievt], tV[ievt], wV[ievt] );
         ic++;
      }
   }   
   biasT /= sumw;
   devT  /= sumw;
   rmsT  /= sumw;
   rmsT  = TMath::Sqrt(rmsT - biasT*biasT);
   mInf  = gTools().GetMutualInformation( *hist );
   mInfT = gTools().GetMutualInformation( *histT );

   delete hist;
   delete histT;

   delete [] rV;
   delete [] tV;
   delete [] wV;

   Data()->SetCurrentType(savedType);   
}


//_______________________________________________________________________
void TMVA::MethodBase::TestMulticlass()
{
   // test multiclass classification 

   ResultsMulticlass* resMulticlass = dynamic_cast<ResultsMulticlass*>(Data()->GetResults(GetMethodName(), Types::kTesting, Types::kMulticlass));
   if (!resMulticlass) Log() << kFATAL<< "unable to create pointer in TestMulticlass, exiting."<<Endl;
   Log() << kINFO << "Determine optimal multiclass cuts for test data..." << Endl;
   for(UInt_t icls = 0; icls<DataInfo().GetNClasses(); ++icls){
      resMulticlass->GetBestMultiClassCuts(icls);
   }
}


//_______________________________________________________________________
void TMVA::MethodBase::TestClassification()
{
   // initialization
   Data()->SetCurrentType(Types::kTesting);

   ResultsClassification* mvaRes = dynamic_cast<ResultsClassification*>
      ( Data()->GetResults(GetMethodName(),Types::kTesting, Types::kClassification) );
   
   // sanity checks: tree must exist, and theVar must be in tree
   if (0==mvaRes && !(GetMethodTypeName().Contains("Cuts"))) {
      Log() << "mvaRes " << mvaRes << " GetMethodTypeName " << GetMethodTypeName()
            << " contains " << !(GetMethodTypeName().Contains("Cuts")) << Endl;
      Log() << kFATAL << "<TestInit> Test variable " << GetTestvarName()
            << " not found in tree" << Endl;
   }
   
   // basic statistics operations are made in base class
   gTools().ComputeStat( GetEventCollection(Types::kTesting), mvaRes->GetValueVector(),
                         fMeanS, fMeanB, fRmsS, fRmsB, fXmin, fXmax, fSignalClass );
   
   // choose reasonable histogram ranges, by removing outliers
   Double_t nrms = 10;
   fXmin = TMath::Max( TMath::Min( fMeanS - nrms*fRmsS, fMeanB - nrms*fRmsB ), fXmin );
   fXmax = TMath::Min( TMath::Max( fMeanS + nrms*fRmsS, fMeanB + nrms*fRmsB ), fXmax );
   
   // determine cut orientation
   fCutOrientation = (fMeanS > fMeanB) ? kPositive : kNegative;
   
   // fill 2 types of histograms for the various analyses
   // this one is for actual plotting
   
   Double_t sxmax = fXmax+0.00001;
   
   // classifier response distributions for training sample
   // MVA plots used for graphics representation (signal)
   TH1* mva_s = new TH1F( GetTestvarName() + "_S",GetTestvarName() + "_S", fNbins, fXmin, sxmax );
   TH1* mva_b = new TH1F( GetTestvarName() + "_B",GetTestvarName() + "_B", fNbins, fXmin, sxmax );
   mvaRes->Store(mva_s, "MVA_S");
   mvaRes->Store(mva_b, "MVA_B");
   mva_s->Sumw2();
   mva_b->Sumw2();

   TH1* proba_s = 0;
   TH1* proba_b = 0;
   TH1* rarity_s = 0;
   TH1* rarity_b = 0;
   if (HasMVAPdfs()) {
      // P(MVA) plots used for graphics representation
      proba_s = new TH1F( GetTestvarName() + "_Proba_S", GetTestvarName() + "_Proba_S", fNbins, 0.0, 1.0 );
      proba_b = new TH1F( GetTestvarName() + "_Proba_B", GetTestvarName() + "_Proba_B", fNbins, 0.0, 1.0 );
      mvaRes->Store(proba_s, "Prob_S");
      mvaRes->Store(proba_b, "Prob_B");
      proba_s->Sumw2();
      proba_b->Sumw2();

      // R(MVA) plots used for graphics representation
      rarity_s = new TH1F( GetTestvarName() + "_Rarity_S", GetTestvarName() + "_Rarity_S", fNbins, 0.0, 1.0 );
      rarity_b = new TH1F( GetTestvarName() + "_Rarity_B", GetTestvarName() + "_Rarity_B", fNbins, 0.0, 1.0 );
      mvaRes->Store(rarity_s, "Rar_S");
      mvaRes->Store(rarity_b, "Rar_B");
      rarity_s->Sumw2();
      rarity_b->Sumw2();
   }
   
   // MVA plots used for efficiency calculations (large number of bins)
   TH1* mva_eff_s = new TH1F( GetTestvarName() + "_S_high", GetTestvarName() + "_S_high", fNbinsH, fXmin, sxmax );
   TH1* mva_eff_b = new TH1F( GetTestvarName() + "_B_high", GetTestvarName() + "_B_high", fNbinsH, fXmin, sxmax );
   mvaRes->Store(mva_eff_s, "MVA_HIGHBIN_S");
   mvaRes->Store(mva_eff_b, "MVA_HIGHBIN_B");
   mva_eff_s->Sumw2();
   mva_eff_b->Sumw2();
   
   // fill the histograms
   ResultsClassification* mvaProb = dynamic_cast<ResultsClassification*>
      (Data()->GetResults( TString("prob_")+GetMethodName(), Types::kTesting, Types::kMaxAnalysisType ) );
   
   Log() << kINFO << "Loop over test events and fill histograms with classifier response..." << Endl;
   if (mvaProb) Log() << kINFO << "Also filling probability and rarity histograms (on request)..." << Endl;
   for (Long64_t ievt=0; ievt<GetNEvents(); ievt++) {
      
      const Event* ev = GetEvent(ievt);
      Float_t v = (*mvaRes)[ievt][0];
      Float_t w = ev->GetWeight();
      
      if (DataInfo().IsSignal(ev)) {
         mva_s ->Fill( v, w );
         if (mvaProb) {
            proba_s->Fill( (*mvaProb)[ievt][0], w );
            rarity_s->Fill( GetRarity( v ), w );
         }
         
         mva_eff_s ->Fill( v, w );
      }
      else {
         mva_b ->Fill( v, w );
         if (mvaProb) {
            proba_b->Fill( (*mvaProb)[ievt][0], w );
            rarity_b->Fill( GetRarity( v ), w );
         }
         mva_eff_b ->Fill( v, w );
      }
   }
   
   gTools().NormHist( mva_s  );
   gTools().NormHist( mva_b  );
   gTools().NormHist( proba_s );
   gTools().NormHist( proba_b );
   gTools().NormHist( rarity_s );
   gTools().NormHist( rarity_b );
   gTools().NormHist( mva_eff_s  );
   gTools().NormHist( mva_eff_b  );
   
   // create PDFs from histograms, using default splines, and no additional smoothing
   if (fSplS) { delete fSplS; fSplS = 0; }
   if (fSplB) { delete fSplB; fSplB = 0; }
   fSplS = new PDF( TString(GetName()) + " PDF Sig", mva_s, PDF::kSpline2 );
   fSplB = new PDF( TString(GetName()) + " PDF Bkg", mva_b, PDF::kSpline2 );
}

//_______________________________________________________________________
void TMVA::MethodBase::WriteStateToStream( std::ostream& tf ) const
{
   // general method used in writing the header of the weight files where
   // the used variables, variable transformation type etc. is specified

   TString prefix = "";
   UserGroup_t * userInfo = gSystem->GetUserInfo();

   tf << prefix << "#GEN -*-*-*-*-*-*-*-*-*-*-*- general info -*-*-*-*-*-*-*-*-*-*-*-" << endl << prefix << endl;
   tf << prefix << "Method         : " << GetMethodTypeName() << "::" << GetMethodName() << endl;
   tf.setf(std::ios::left);
   tf << prefix << "TMVA Release   : " << std::setw(10) << GetTrainingTMVAVersionString() << "    ["
      << GetTrainingTMVAVersionCode() << "]" << endl;
   tf << prefix << "ROOT Release   : " << std::setw(10) << GetTrainingROOTVersionString() << "    ["
      << GetTrainingROOTVersionCode() << "]" << endl;
   tf << prefix << "Creator        : " << userInfo->fUser << endl;
   tf << prefix << "Date           : "; TDatime *d = new TDatime; tf << d->AsString() << endl; delete d;
   tf << prefix << "Host           : " << gSystem->GetBuildNode() << endl;
   tf << prefix << "Dir            : " << gSystem->WorkingDirectory() << endl;
   tf << prefix << "Training events: " << Data()->GetNTrainingEvents() << endl;

   TString analysisType(((const_cast<TMVA::MethodBase*>(this)->GetAnalysisType()==Types::kRegression) ? "Regression" : "Classification"));

   tf << prefix << "Analysis type  : " << "[" << ((GetAnalysisType()==Types::kRegression) ? "Regression" : "Classification") << "]" << endl;
   tf << prefix << endl;

   delete userInfo;

   // First write all options
   tf << prefix << endl << prefix << "#OPT -*-*-*-*-*-*-*-*-*-*-*-*- options -*-*-*-*-*-*-*-*-*-*-*-*-" << endl << prefix << endl;
   WriteOptionsToStream( tf, prefix );
   tf << prefix << endl;

   // Second write variable info
   tf << prefix << endl << prefix << "#VAR -*-*-*-*-*-*-*-*-*-*-*-* variables *-*-*-*-*-*-*-*-*-*-*-*-" << endl << prefix << endl;
   WriteVarsToStream( tf, prefix );
   tf << prefix << endl;
}

//_______________________________________________________________________
void TMVA::MethodBase::AddInfoItem( void* gi, const TString& name, const TString& value) const 
{
   // xml writing
   void* it = gTools().AddChild(gi,"Info");
   gTools().AddAttr(it,"name", name);
   gTools().AddAttr(it,"value", value);
}

//_______________________________________________________________________
void TMVA::MethodBase::AddOutput( Types::ETreeType type, Types::EAnalysisType analysisType ) {
   if (analysisType == Types::kRegression) {
      AddRegressionOutput( type );
   } else if (analysisType == Types::kMulticlass ){
      AddMulticlassOutput( type );
   } else {
      AddClassifierOutput( type );
      if (HasMVAPdfs())
         AddClassifierOutputProb( type );
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::WriteStateToXML( void* parent ) const
{
   // general method used in writing the header of the weight files where
   // the used variables, variable transformation type etc. is specified

   if (!parent) return;

   UserGroup_t* userInfo = gSystem->GetUserInfo();

   void* gi = gTools().AddChild(parent, "GeneralInfo");
   AddInfoItem( gi, "TMVA Release", GetTrainingTMVAVersionString() + " [" + gTools().StringFromInt(GetTrainingTMVAVersionCode()) + "]" );
   AddInfoItem( gi, "ROOT Release", GetTrainingROOTVersionString() + " [" + gTools().StringFromInt(GetTrainingROOTVersionCode()) + "]");
   AddInfoItem( gi, "Creator", userInfo->fUser);
   TDatime dt; AddInfoItem( gi, "Date", dt.AsString());
   AddInfoItem( gi, "Host", gSystem->GetBuildNode() );
   AddInfoItem( gi, "Dir", gSystem->WorkingDirectory());
   AddInfoItem( gi, "Training events", gTools().StringFromInt(Data()->GetNTrainingEvents()));
   AddInfoItem( gi, "TrainingTime", gTools().StringFromDouble(const_cast<TMVA::MethodBase*>(this)->GetTrainTime()));

   Types::EAnalysisType aType = const_cast<TMVA::MethodBase*>(this)->GetAnalysisType();
   TString analysisType((aType==Types::kRegression) ? "Regression" :
                        (aType==Types::kMulticlass ? "Multiclass" : "Classification"));
   AddInfoItem( gi, "AnalysisType", analysisType );
   delete userInfo;

   // write options
   AddOptionsXMLTo( parent );

   // write variable info
   AddVarsXMLTo( parent );

   // write spectator info
   if(!fDisableWriting)
      AddSpectatorsXMLTo( parent );

   // write class info if in multiclass mode
   if(DoMulticlass())
      AddClassesXMLTo(parent);
   
   // write target info if in regression mode
   if(DoRegression())
      AddTargetsXMLTo(parent);

   // write transformations
   GetTransformationHandler().AddXMLTo( parent );

   // write MVA variable distributions
   void* pdfs = gTools().AddChild(parent, "MVAPdfs");
   if (fMVAPdfS) fMVAPdfS->AddXMLTo(pdfs);
   if (fMVAPdfB) fMVAPdfB->AddXMLTo(pdfs);

   // write weights
   AddWeightsXMLTo( parent );
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadStateFromStream( TFile& rf )
{
   // write reference MVA distributions (and other information)
   // to a ROOT type weight file

   Bool_t addDirStatus = TH1::AddDirectoryStatus();
   TH1::AddDirectory( 0 ); // this avoids the binding of the hists in PDF to the current ROOT file
   fMVAPdfS = (TMVA::PDF*)rf.Get( "MVA_PDF_Signal" );
   fMVAPdfB = (TMVA::PDF*)rf.Get( "MVA_PDF_Background" );

   TH1::AddDirectory( addDirStatus );

   ReadWeightsFromStream( rf );

   SetTestvarName();
}

//_______________________________________________________________________
void TMVA::MethodBase::WriteStateToFile() const
{
   // write options and weights to file
   // note that each one text file for the main configuration information
   // and one ROOT file for ROOT objects are created

   // ---- create the text file
   TString tfname( GetWeightFileName() );

   // writing xml file
   TString xmlfname( tfname ); xmlfname.ReplaceAll( ".txt", ".xml" );
   Log() << kINFO << "Creating weight file in xml format: "
         << gTools().Color("lightblue") << xmlfname << gTools().Color("reset") << Endl;
   void* doc      = gTools().xmlengine().NewDoc();
   void* rootnode = gTools().AddChild(0,"MethodSetup", "", true);
   gTools().xmlengine().DocSetRootElement(doc,rootnode);
   gTools().AddAttr(rootnode,"Method", GetMethodTypeName() + "::" + GetMethodName());
   WriteStateToXML(rootnode);
   gTools().xmlengine().SaveDoc(doc,xmlfname);
   gTools().xmlengine().FreeDoc(doc);
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadStateFromFile()
{
   // Function to write options and weights to file

   // get the filename

   TString tfname(GetWeightFileName());

   Log() << kINFO << "Reading weight file: "
         << gTools().Color("lightblue") << tfname << gTools().Color("reset") << Endl;

   if (tfname.EndsWith(".xml") ) {
      void* doc = gTools().xmlengine().ParseFile(tfname);
      void* rootnode = gTools().xmlengine().DocGetRootElement(doc); // node "MethodSetup"
      ReadStateFromXML(rootnode);
      gTools().xmlengine().FreeDoc(doc);
   }
   else {
      filebuf fb;
      fb.open(tfname.Data(),ios::in);
      if (!fb.is_open()) { // file not found --> Error
         Log() << kFATAL << "<ReadStateFromFile> "
               << "Unable to open input weight file: " << tfname << Endl;
      }
      istream fin(&fb);
      ReadStateFromStream(fin);
      fb.close();
   }
   if (!fTxtWeightsOnly) {
      // ---- read the ROOT file
      TString rfname( tfname ); rfname.ReplaceAll( ".txt", ".root" );
      Log() << kINFO << "Reading root weight file: "
            << gTools().Color("lightblue") << rfname << gTools().Color("reset") << Endl;
      TFile* rfile = TFile::Open( rfname, "READ" );
      ReadStateFromStream( *rfile );
      rfile->Close();
   }
}
//_______________________________________________________________________
void TMVA::MethodBase::ReadStateFromXMLString( const char* xmlstr ) {
   // for reading from memory

#if (ROOT_SVN_REVISION >= 32259) && (ROOT_VERSION_CODE >= 334336) // 5.26/00
   void* doc = gTools().xmlengine().ParseString(xmlstr);
   void* rootnode = gTools().xmlengine().DocGetRootElement(doc); // node "MethodSetup"
   ReadStateFromXML(rootnode);
   gTools().xmlengine().FreeDoc(doc);
#else
   Log() << kFATAL << "Method MethodBase::ReadStateFromXMLString( const char* xmlstr = " 
         << xmlstr << " ) is not available for ROOT versions prior to 5.26/00." << Endl;
#endif

   return;
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadStateFromXML( void* methodNode )
{
   TString fullMethodName;
   gTools().ReadAttr( methodNode, "Method", fullMethodName );
   fMethodName = fullMethodName(fullMethodName.Index("::")+2,fullMethodName.Length());

   // update logger
   Log().SetSource( GetName() );
   Log() << kINFO << "Read method \"" << GetMethodName() << "\" of type \"" << GetMethodTypeName() << "\"" << Endl;

   // after the method name is read, the testvar can be set
   SetTestvarName();

   TString nodeName("");
   void* ch = gTools().GetChild(methodNode);
   while (ch!=0) {
      nodeName = TString( gTools().GetName(ch) );

      if (nodeName=="GeneralInfo") {
         // read analysis type

         TString name(""),val("");
         void* antypeNode = gTools().GetChild(ch);
         while (antypeNode) {
            gTools().ReadAttr( antypeNode, "name",   name );

            if (name == "TrainingTime")
               gTools().ReadAttr( antypeNode, "value",  fTrainTime );

            if (name == "AnalysisType") {
               gTools().ReadAttr( antypeNode, "value",  val );
               val.ToLower();
               if      (val == "regression" )     SetAnalysisType( Types::kRegression );
               else if (val == "classification" ) SetAnalysisType( Types::kClassification );
               else if (val == "multiclass" )     SetAnalysisType( Types::kMulticlass );
               else Log() << kFATAL << "Analysis type " << val << " is not known." << Endl;
            }

            if (name == "TMVA Release" || name == "TMVA" ){
               TString s;
               gTools().ReadAttr( antypeNode, "value", s);
               fTMVATrainingVersion = TString(s(s.Index("[")+1,s.Index("]")-s.Index("[")-1)).Atoi();
               Log() << kINFO << "MVA method was trained with TMVA Version: " << GetTrainingTMVAVersionString() << Endl;
            }

            if (name == "ROOT Release" || name == "ROOT" ){
               TString s;
               gTools().ReadAttr( antypeNode, "value", s);
               fROOTTrainingVersion = TString(s(s.Index("[")+1,s.Index("]")-s.Index("[")-1)).Atoi();
               Log() << kINFO << "MVA method was trained with ROOT Version: " << GetTrainingROOTVersionString() << Endl;
            }
            antypeNode = gTools().GetNextChild(antypeNode);
         }
      }
      else if (nodeName=="Options") {
         ReadOptionsFromXML(ch);
         ParseOptions();

      }
      else if (nodeName=="Variables") {
         ReadVariablesFromXML(ch);
      }
      else if (nodeName=="Spectators") {
         ReadSpectatorsFromXML(ch);
      }
      else if (nodeName=="Classes") {
         if(DataInfo().GetNClasses()==0 && DoMulticlass())
            ReadClassesFromXML(ch);
      }
      else if (nodeName=="Targets") {
         if(DataInfo().GetNTargets()==0 && DoRegression())
            ReadTargetsFromXML(ch);
      }
      else if (nodeName=="Transformations") {
         GetTransformationHandler().ReadFromXML(ch);
      }
      else if (nodeName=="MVAPdfs") {
         TString pdfname;
         if (fMVAPdfS) { delete fMVAPdfS; fMVAPdfS=0; }
         if (fMVAPdfB) { delete fMVAPdfB; fMVAPdfB=0; }
         void* pdfnode = gTools().GetChild(ch);
         if (pdfnode) {
            gTools().ReadAttr(pdfnode, "Name", pdfname);
            fMVAPdfS = new PDF(pdfname);
            fMVAPdfS->ReadXML(pdfnode);
            pdfnode = gTools().GetNextChild(pdfnode);
            gTools().ReadAttr(pdfnode, "Name", pdfname);
            fMVAPdfB = new PDF(pdfname);
            fMVAPdfB->ReadXML(pdfnode);
         }
      }
      else if (nodeName=="Weights") {
         ReadWeightsFromXML(ch);
      }
      else {
         Log() << kWARNING << "Unparsed XML node: '" << nodeName << "'" << Endl;
      }
      ch = gTools().GetNextChild(ch);

   }

   // update transformation handler
   if (GetTransformationHandler().GetCallerName() == "") GetTransformationHandler().SetCallerName( GetName() );
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadStateFromStream( std::istream& fin )
{
   // read the header from the weight files of the different MVA methods
   char buf[512];

   // when reading from stream, we assume the files are produced with TMVA<=397
   SetAnalysisType(Types::kClassification);


   // first read the method name
   GetLine(fin,buf);
   while (!TString(buf).BeginsWith("Method")) GetLine(fin,buf);
   TString namestr(buf);

   TString methodType = namestr(0,namestr.Index("::"));
   methodType = methodType(methodType.Last(' '),methodType.Length());
   methodType = methodType.Strip(TString::kLeading);

   TString methodName = namestr(namestr.Index("::")+2,namestr.Length());
   methodName = methodName.Strip(TString::kLeading);
   if (methodName == "") methodName = methodType;
   fMethodName  = methodName;

   Log() << kINFO << "Read method \"" << GetMethodName() << "\" of type \"" << GetMethodTypeName() << "\"" << Endl;

   // update logger
   Log().SetSource( GetName() );

   // now the question is whether to read the variables first or the options (well, of course the order
   // of writing them needs to agree)
   //
   // the option "Decorrelation" is needed to decide if the variables we
   // read are decorrelated or not
   //
   // the variables are needed by some methods (TMLP) to build the NN
   // which is done in ProcessOptions so for the time being we first Read and Parse the options then
   // we read the variables, and then we process the options

   // now read all options
   GetLine(fin,buf);
   while (!TString(buf).BeginsWith("#OPT")) GetLine(fin,buf);
   ReadOptionsFromStream(fin);
   ParseOptions();

   // Now read variable info
   fin.getline(buf,512);
   while (!TString(buf).BeginsWith("#VAR")) fin.getline(buf,512);
   ReadVarsFromStream(fin);

   // now we process the options (of the derived class)
   ProcessOptions();

   if(IsNormalised()) {
      VariableNormalizeTransform* norm = (VariableNormalizeTransform*)
         GetTransformationHandler().AddTransformation( new VariableNormalizeTransform(DataInfo()), -1 );
      norm->BuildTransformationFromVarInfo( DataInfo().GetVariableInfos() );
   }
   VariableTransformBase *varTrafo(0), *varTrafo2(0);
   if ( fVarTransformString == "None") {
      if (fUseDecorr)
         varTrafo = GetTransformationHandler().AddTransformation( new VariableDecorrTransform(DataInfo()), -1 );
   } else if ( fVarTransformString == "Decorrelate" ) {
      varTrafo = GetTransformationHandler().AddTransformation( new VariableDecorrTransform(DataInfo()), -1 );
   } else if ( fVarTransformString == "PCA"  ) {
      varTrafo = GetTransformationHandler().AddTransformation( new VariablePCATransform(DataInfo()), -1 );
   } else if ( fVarTransformString == "Uniform" ) {
      varTrafo  = GetTransformationHandler().AddTransformation( new VariableGaussTransform(DataInfo(),"Uniform"), -1 );
   } else if ( fVarTransformString == "Gauss" ) {
      varTrafo  = GetTransformationHandler().AddTransformation( new VariableGaussTransform(DataInfo()), -1 );
   } else if ( fVarTransformString == "GaussDecorr" ) {
      varTrafo  = GetTransformationHandler().AddTransformation( new VariableGaussTransform(DataInfo()), -1 );
      varTrafo2 = GetTransformationHandler().AddTransformation( new VariableDecorrTransform(DataInfo()), -1 );
   } else {
      Log() << kFATAL << "<ProcessOptions> Variable transform '"
            << fVarTransformString << "' unknown." << Endl;
   }
   // Now read decorrelation matrix if available
   if (GetTransformationHandler().GetTransformationList().GetSize() > 0) {
      fin.getline(buf,512);
      while (!TString(buf).BeginsWith("#MAT")) fin.getline(buf,512);
      if(varTrafo) {
         TString trafo(fVariableTransformTypeString); trafo.ToLower();
         varTrafo->ReadTransformationFromStream(fin, trafo );
      }
      if(varTrafo2) {
         TString trafo(fVariableTransformTypeString); trafo.ToLower();
         varTrafo2->ReadTransformationFromStream(fin, trafo );
      }
   }


   if (HasMVAPdfs()) {
      // Now read the MVA PDFs
      fin.getline(buf,512);
      while (!TString(buf).BeginsWith("#MVAPDFS")) fin.getline(buf,512);
      if (fMVAPdfS != 0) { delete fMVAPdfS; fMVAPdfS = 0; }
      if (fMVAPdfB != 0) { delete fMVAPdfB; fMVAPdfB = 0; }
      fMVAPdfS = new PDF(TString(GetName()) + " MVA PDF Sig");
      fMVAPdfB = new PDF(TString(GetName()) + " MVA PDF Bkg");
      fMVAPdfS->SetReadingVersion( GetTrainingTMVAVersionCode() );
      fMVAPdfB->SetReadingVersion( GetTrainingTMVAVersionCode() );

      fin >> *fMVAPdfS;
      fin >> *fMVAPdfB;
   }

   // Now read weights
   fin.getline(buf,512);
   while (!TString(buf).BeginsWith("#WGT")) fin.getline(buf,512);
   fin.getline(buf,512);
   ReadWeightsFromStream( fin );;

   // update transformation handler
   if (GetTransformationHandler().GetCallerName() == "") GetTransformationHandler().SetCallerName( GetName() );

}

//_______________________________________________________________________
void TMVA::MethodBase::WriteVarsToStream( std::ostream& o, const TString& prefix ) const
{
   // write the list of variables (name, min, max) for a given data
   // transformation method to the stream
   o << prefix << "NVar " << DataInfo().GetNVariables() << endl;
   std::vector<VariableInfo>::const_iterator varIt = DataInfo().GetVariableInfos().begin();
   for (; varIt!=DataInfo().GetVariableInfos().end(); varIt++) { o << prefix; varIt->WriteToStream(o); }
   o << prefix << "NSpec " << DataInfo().GetNSpectators() << endl;
   varIt = DataInfo().GetSpectatorInfos().begin();
   for (; varIt!=DataInfo().GetSpectatorInfos().end(); varIt++) { o << prefix; varIt->WriteToStream(o); }
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadVarsFromStream( std::istream& istr )
{
   // Read the variables (name, min, max) for a given data
   // transformation method from the stream. In the stream we only
   // expect the limits which will be set
   TString dummy;
   UInt_t readNVar;
   istr >> dummy >> readNVar;

   if (readNVar!=DataInfo().GetNVariables()) {
      Log() << kFATAL << "You declared "<< DataInfo().GetNVariables() << " variables in the Reader"
            << " while there are " << readNVar << " variables declared in the file"
            << Endl;
   }

   // we want to make sure all variables are read in the order they are defined
   VariableInfo varInfo;
   std::vector<VariableInfo>::iterator varIt = DataInfo().GetVariableInfos().begin();
   int varIdx = 0;
   for (; varIt!=DataInfo().GetVariableInfos().end(); varIt++, varIdx++) {
      varInfo.ReadFromStream(istr);
      if (varIt->GetExpression() == varInfo.GetExpression()) {
         varInfo.SetExternalLink((*varIt).GetExternalLink());
         (*varIt) = varInfo;
      }
      else {
         Log() << kINFO << "ERROR in <ReadVarsFromStream>" << Endl;
         Log() << kINFO << "The definition (or the order) of the variables found in the input file is"  << Endl;
         Log() << kINFO << "is not the same as the one declared in the Reader (which is necessary for" << Endl;
         Log() << kINFO << "the correct working of the method):" << Endl;
         Log() << kINFO << "   var #" << varIdx <<" declared in Reader: " << varIt->GetExpression() << Endl;
         Log() << kINFO << "   var #" << varIdx <<" declared in file  : " << varInfo.GetExpression() << Endl;
         Log() << kFATAL << "The expression declared to the Reader needs to be checked (name or order are wrong)" << Endl;
      }
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::AddVarsXMLTo( void* parent ) const 
{
   // write variable info to XML 
   void* vars = gTools().AddChild(parent, "Variables");
   gTools().AddAttr( vars, "NVar", gTools().StringFromInt(DataInfo().GetNVariables()) );

   for (UInt_t idx=0; idx<DataInfo().GetVariableInfos().size(); idx++) {
      VariableInfo& vi = DataInfo().GetVariableInfos()[idx];
      void* var = gTools().AddChild( vars, "Variable" );
      gTools().AddAttr( var, "VarIndex", idx );
      vi.AddToXML( var );
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::AddSpectatorsXMLTo( void* parent ) const 
{
   // write spectator info to XML 
   void* specs = gTools().AddChild(parent, "Spectators");

   UInt_t writeIdx=0;
   for (UInt_t idx=0; idx<DataInfo().GetSpectatorInfos().size(); idx++) {

      VariableInfo& vi = DataInfo().GetSpectatorInfos()[idx];

      // we do not want to write spectators that are category-cuts,
      // except if the method is the category method and the spectators belong to it
      if( vi.GetVarType()=='C' )
         continue;

      void* spec = gTools().AddChild( specs, "Spectator" );
      gTools().AddAttr( spec, "SpecIndex", writeIdx++ );
      vi.AddToXML( spec );
   }
   gTools().AddAttr( specs, "NSpec", gTools().StringFromInt(writeIdx) );
}

//_______________________________________________________________________
void TMVA::MethodBase::AddClassesXMLTo( void* parent ) const 
{
   // write class info to XML 
   void* targets = gTools().AddChild(parent, "Classes");
   gTools().AddAttr( targets, "NClass", gTools().StringFromInt(DataInfo().GetNClasses()) );

}
//_______________________________________________________________________
void TMVA::MethodBase::AddTargetsXMLTo( void* parent ) const 
{
   // write target info to XML 
   void* targets = gTools().AddChild(parent, "Targets");
   gTools().AddAttr( targets, "NTrgt", gTools().StringFromInt(DataInfo().GetNTargets()) );

   for (UInt_t idx=0; idx<DataInfo().GetTargetInfos().size(); idx++) {
      VariableInfo& vi = DataInfo().GetTargetInfos()[idx];
      void* tar = gTools().AddChild( targets, "Target" );
      gTools().AddAttr( tar, "TargetIndex", idx );
      vi.AddToXML( tar );
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadVariablesFromXML( void* varnode ) 
{
   // read variable info from XML
   UInt_t readNVar;
   gTools().ReadAttr( varnode, "NVar", readNVar);

   if (readNVar!=DataInfo().GetNVariables()) {
      Log() << kFATAL << "You declared "<< DataInfo().GetNVariables() << " variables in the Reader"
            << " while there are " << readNVar << " variables declared in the file"
            << Endl;
   }

   // we want to make sure all variables are read in the order they are defined
   VariableInfo readVarInfo, existingVarInfo;
   int varIdx = 0;
   void* ch = gTools().GetChild(varnode);
   while (ch) {
      gTools().ReadAttr( ch, "VarIndex", varIdx);
      existingVarInfo = DataInfo().GetVariableInfos()[varIdx];
      readVarInfo.ReadFromXML(ch);
      
      if (existingVarInfo.GetExpression() == readVarInfo.GetExpression()) {
         readVarInfo.SetExternalLink(existingVarInfo.GetExternalLink());
         existingVarInfo = readVarInfo;
      } 
      else {
         Log() << kINFO << "ERROR in <ReadVariablesFromXML>" << Endl;
         Log() << kINFO << "The definition (or the order) of the variables found in the input file is"  << Endl;
         Log() << kINFO << "is not the same as the one declared in the Reader (which is necessary for" << Endl;
         Log() << kINFO << "the correct working of the method):" << Endl;
         Log() << kINFO << "   var #" << varIdx <<" declared in Reader: " << existingVarInfo.GetExpression() << Endl;
         Log() << kINFO << "   var #" << varIdx <<" declared in file  : " << readVarInfo.GetExpression() << Endl;
         Log() << kFATAL << "The expression declared to the Reader needs to be checked (name or order are wrong)" << Endl;
      }
      ch = gTools().GetNextChild(ch);
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadSpectatorsFromXML( void* specnode ) 
{
   // read spectator info from XML
   UInt_t readNSpec;
   gTools().ReadAttr( specnode, "NSpec", readNSpec);

   if (readNSpec!=DataInfo().GetNSpectators(kFALSE)) {
      Log() << kFATAL << "You declared "<< DataInfo().GetNSpectators(kFALSE) << " spectators in the Reader"
            << " while there are " << readNSpec << " spectators declared in the file"
            << Endl;
   }

   // we want to make sure all variables are read in the order they are defined
   VariableInfo readSpecInfo, existingSpecInfo;
   int specIdx = 0;
   void* ch = gTools().GetChild(specnode);
   while (ch) {
      gTools().ReadAttr( ch, "SpecIndex", specIdx);
      existingSpecInfo = DataInfo().GetSpectatorInfos()[specIdx];
      readSpecInfo.ReadFromXML(ch);
      
      if (existingSpecInfo.GetExpression() == readSpecInfo.GetExpression()) {
         readSpecInfo.SetExternalLink(existingSpecInfo.GetExternalLink());
         existingSpecInfo = readSpecInfo;
      } 
      else {
         Log() << kINFO << "ERROR in <ReadVariablesFromXML>" << Endl;
         Log() << kINFO << "The definition (or the order) of the variables found in the input file is"  << Endl;
         Log() << kINFO << "is not the same as the one declared in the Reader (which is necessary for" << Endl;
         Log() << kINFO << "the correct working of the method):" << Endl;
         Log() << kINFO << "   var #" << specIdx <<" declared in Reader: " << existingSpecInfo.GetExpression() << Endl;
         Log() << kINFO << "   var #" << specIdx <<" declared in file  : " << readSpecInfo.GetExpression() << Endl;
         Log() << kFATAL << "The expression declared to the Reader needs to be checked (name or order are wrong)" << Endl;
      }
      ch = gTools().GetNextChild(ch);
   }
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadClassesFromXML( void* clsnode ) 
{
   // read number of classes from XML
   UInt_t readNCls;
   // coverity[tainted_data_argument]
   gTools().ReadAttr( clsnode, "NClass", readNCls);

   for(UInt_t icls = 0; icls<readNCls;++icls){
      TString classname = Form("class%i",icls);
      DataInfo().AddClass(classname);

   }
}

//_______________________________________________________________________
void TMVA::MethodBase::ReadTargetsFromXML( void* tarnode ) 
{
   // read target info from XML
   UInt_t readNTar;
   gTools().ReadAttr( tarnode, "NTrgt", readNTar);

   int tarIdx = 0;
   TString expression;
   void* ch = gTools().GetChild(tarnode);
   while (ch) {
      gTools().ReadAttr( ch, "TargetIndex", tarIdx);
      gTools().ReadAttr( ch, "Expression", expression);
      DataInfo().AddTarget(expression,"","",0,0);

      ch = gTools().GetNextChild(ch);
   }
}

//_______________________________________________________________________
TDirectory* TMVA::MethodBase::BaseDir() const
{
   // returns the ROOT directory where info/histograms etc of the
   // corresponding MVA method instance are stored

   if (fBaseDir != 0) return fBaseDir;
   Log()<<kDEBUG<<" Base Directory for " << GetMethodTypeName() << " not set yet --> check if already there.." <<Endl;

   TDirectory* methodDir = MethodBaseDir();
   if (methodDir==0)
      Log() << kFATAL << "MethodBase::BaseDir() - MethodBaseDir() return a NULL pointer!" << Endl;

   TDirectory* dir = 0;

   TString defaultDir = GetMethodName();

   TObject* o = methodDir->FindObject(defaultDir);
   if (o!=0 && o->InheritsFrom(TDirectory::Class())) dir = (TDirectory*)o;

   if (dir != 0) {
      Log()<<kDEBUG<<" Base Directory for " << GetMethodName() << " existed, return it.." <<Endl;
      return dir;
   }

   Log()<<kDEBUG<<" Base Directory for " << GetMethodName() << " does not exist yet--> created it" <<Endl;
   TDirectory *sdir = methodDir->mkdir(defaultDir);

   // write weight file name into target file
   sdir->cd();
   TObjString wfilePath( gSystem->WorkingDirectory() );
   TObjString wfileName( GetWeightFileName() );
   wfilePath.Write( "TrainingPath" );
   wfileName.Write( "WeightFileName" );

   return sdir;
}

//_______________________________________________________________________
TDirectory* TMVA::MethodBase::MethodBaseDir() const
{
   // returns the ROOT directory where all instances of the
   // corresponding MVA method are stored

   if (fMethodBaseDir != 0) return fMethodBaseDir;

   Log()<<kDEBUG<<" Base Directory for " << GetMethodTypeName() << " not set yet --> check if already there.." <<Endl;

   const TString dirName(Form("Method_%s",GetMethodTypeName().Data()));

   TDirectory * dir = Factory::RootBaseDir()->GetDirectory(dirName);
   if (dir != 0){
      Log()<<kDEBUG<<" Base Directory for " << GetMethodTypeName() << " existed, return it.." <<Endl;
      return dir;
   }

   Log()<<kDEBUG<<" Base Directory for " << GetMethodTypeName() << " does not exist yet--> created it" <<Endl;
   fMethodBaseDir = Factory::RootBaseDir()->mkdir(dirName,Form("Directory for all %s methods", GetMethodTypeName().Data()));

   Log()<<kDEBUG<<"Return from MethodBaseDir() after creating base directory "<<Endl;
   return fMethodBaseDir;
}

//_______________________________________________________________________
void TMVA::MethodBase::SetWeightFileDir( TString fileDir )
{
   // set directory of weight file

   fFileDir = fileDir;
   gSystem->MakeDirectory( fFileDir );
}

//_______________________________________________________________________
void TMVA::MethodBase::SetWeightFileName( TString theWeightFile)
{
   // set the weight file name (depreciated)
   fWeightFile = theWeightFile;
}

//_______________________________________________________________________
TString TMVA::MethodBase::GetWeightFileName() const
{
   // retrieve weight file name
   if (fWeightFile!="") return fWeightFile;

   // the default consists of
   // directory/jobname_methodname_suffix.extension.{root/txt}
   TString suffix = "";
   TString wFileDir(GetWeightFileDir());
   return ( wFileDir + (wFileDir[wFileDir.Length()-1]=='/' ? "" : "/") 
	    + GetJobName() + "_" + GetMethodName() +
            suffix + "." + gConfig().GetIONames().fWeightFileExtension + ".xml" );
}

//_______________________________________________________________________
void TMVA::MethodBase::WriteEvaluationHistosToFile(Types::ETreeType treetype)
{
   // writes all MVA evaluation histograms to file
   BaseDir()->cd();

   // write MVA PDFs to file - if exist
   if (0 != fMVAPdfS) {
      fMVAPdfS->GetOriginalHist()->Write();
      fMVAPdfS->GetSmoothedHist()->Write();
      fMVAPdfS->GetPDFHist()->Write();
   }
   if (0 != fMVAPdfB) {
      fMVAPdfB->GetOriginalHist()->Write();
      fMVAPdfB->GetSmoothedHist()->Write();
      fMVAPdfB->GetPDFHist()->Write();
   }

   // write result-histograms
   Results* results = Data()->GetResults( GetMethodName(), treetype, Types::kMaxAnalysisType );
   if (!results)
      Log() << kFATAL << "<WriteEvaluationHistosToFile> Unknown result: "
            << GetMethodName() << (treetype==Types::kTraining?"/kTraining":"/kTesting") << "/kMaxAnalysisType" << Endl;
   results->GetStorage()->Write();
   if(treetype==Types::kTesting)
      GetTransformationHandler().PlotVariables( GetEventCollection( Types::kTesting ), BaseDir() );
}

//_______________________________________________________________________
void  TMVA::MethodBase::WriteMonitoringHistosToFile( void ) const
{
   // write special monitoring histograms to file
   // dummy implementation here -----------------
}

//_______________________________________________________________________
Bool_t TMVA::MethodBase::GetLine(std::istream& fin, char* buf )
{
   // reads one line from the input stream
   // checks for certain keywords and interprets
   // the line if keywords are found
   fin.getline(buf,512);
   TString line(buf);
   if (line.BeginsWith("TMVA Release")) {
      Ssiz_t start = line.First('[')+1;
      Ssiz_t length = line.Index("]",start)-start;
      TString code = line(start,length);
      std::stringstream s(code.Data());
      s >> fTMVATrainingVersion;
      Log() << kINFO << "MVA method was trained with TMVA Version: " << GetTrainingTMVAVersionString() << Endl;
   }
   if (line.BeginsWith("ROOT Release")) {
      Ssiz_t start = line.First('[')+1;
      Ssiz_t length = line.Index("]",start)-start;
      TString code = line(start,length);
      std::stringstream s(code.Data());
      s >> fROOTTrainingVersion;
      Log() << kINFO << "MVA method was trained with ROOT Version: " << GetTrainingROOTVersionString() << Endl;
   }
   if (line.BeginsWith("Analysis type")) {
      Ssiz_t start = line.First('[')+1;
      Ssiz_t length = line.Index("]",start)-start;
      TString code = line(start,length);
      std::stringstream s(code.Data());
      std::string analysisType;
      s >> analysisType;
      if      (analysisType == "regression"     || analysisType == "Regression")     SetAnalysisType( Types::kRegression );
      else if (analysisType == "classification" || analysisType == "Classification") SetAnalysisType( Types::kClassification );
      else if (analysisType == "multiclass"     || analysisType == "Multiclass")     SetAnalysisType( Types::kMulticlass );
      else Log() << kFATAL << "Analysis type " << analysisType << " from weight-file not known!" << std::endl;

      Log() << kINFO << "Method was trained for " 
            << (GetAnalysisType() == Types::kRegression ? "Regression" : 
                (GetAnalysisType() == Types::kMulticlass ? "Multiclass" : "Classification")) << Endl;
   }

   return true;
}

//_______________________________________________________________________
void TMVA::MethodBase::CreateMVAPdfs()
{
   // Create PDFs of the MVA output variables

   Data()->SetCurrentType(Types::kTraining);

   ResultsClassification * mvaRes = dynamic_cast<ResultsClassification*>
      ( Data()->GetResults(GetMethodName(), Types::kTraining, Types::kClassification) );

   if (mvaRes==0 || mvaRes->GetSize()==0) {
      Log() << kFATAL << "<CreateMVAPdfs> No result of classifier testing available" << Endl;
   }

   Double_t minVal = *std::min_element(mvaRes->GetValueVector()->begin(),mvaRes->GetValueVector()->end());
   Double_t maxVal = *std::max_element(mvaRes->GetValueVector()->begin(),mvaRes->GetValueVector()->end());

   // create histograms that serve as basis to create the MVA Pdfs
   TH1* histMVAPdfS = new TH1F( GetMethodTypeName() + "_tr_S", GetMethodTypeName() + "_tr_S",
                                fMVAPdfS->GetHistNBins( mvaRes->GetSize() ), minVal, maxVal );
   TH1* histMVAPdfB = new TH1F( GetMethodTypeName() + "_tr_B", GetMethodTypeName() + "_tr_B",
                                fMVAPdfB->GetHistNBins( mvaRes->GetSize() ), minVal, maxVal );


   // compute sum of weights properly
   histMVAPdfS->Sumw2();
   histMVAPdfB->Sumw2();

   // fill histograms
   for (UInt_t ievt=0; ievt<mvaRes->GetSize(); ievt++) {
      Double_t theVal    = mvaRes->GetValueVector()->at(ievt);
      Double_t theWeight = Data()->GetEvent(ievt)->GetWeight();

      if (DataInfo().IsSignal(Data()->GetEvent(ievt))) histMVAPdfS->Fill( theVal, theWeight );
      else                                             histMVAPdfB->Fill( theVal, theWeight );
   }

   gTools().NormHist( histMVAPdfS );
   gTools().NormHist( histMVAPdfB );

   // momentary hack for ROOT problem
   histMVAPdfS->Write();
   histMVAPdfB->Write();

   // create PDFs
   fMVAPdfS->BuildPDF   ( histMVAPdfS );
   fMVAPdfB->BuildPDF   ( histMVAPdfB );
   fMVAPdfS->ValidatePDF( histMVAPdfS );
   fMVAPdfB->ValidatePDF( histMVAPdfB );

   if (DataInfo().GetNClasses() == 2) { // TODO: this is an ugly hack.. adapt this to new framework
      Log() << kINFO
            << Form( "<CreateMVAPdfs> Separation from histogram (PDF): %1.3f (%1.3f)",
                     GetSeparation( histMVAPdfS, histMVAPdfB ), GetSeparation( fMVAPdfS, fMVAPdfB ) )
            << Endl;
   }

   delete histMVAPdfS;
   delete histMVAPdfB;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetProba( Double_t mvaVal, Double_t ap_sig )
{
   // compute likelihood ratio
   if (!fMVAPdfS || !fMVAPdfB) {
      Log() << kWARNING << "<GetProba> MVA PDFs for Signal and Background don't exist" << Endl;
      return -1.0;
   }
   Double_t p_s = fMVAPdfS->GetVal( mvaVal );
   Double_t p_b = fMVAPdfB->GetVal( mvaVal );

   Double_t denom = p_s*ap_sig + p_b*(1 - ap_sig);

   return (denom > 0) ? (p_s*ap_sig) / denom : -1;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetRarity( Double_t mvaVal, Types::ESBType reftype ) const
{
   // compute rarity:
   // R(x) = Integrate_[-oo..x] { PDF(x') dx' }
   // where PDF(x) is the PDF of the classifier's signal or background distribution

   if ((reftype == Types::kSignal && !fMVAPdfS) || (reftype == Types::kBackground && !fMVAPdfB)) {
      Log() << kWARNING << "<GetRarity> Required MVA PDF for Signal or Backgroud does not exist: "
            << "select option \"CreateMVAPdfs\"" << Endl;
      return 0.0;
   }

   PDF* thePdf = ((reftype == Types::kSignal) ? fMVAPdfS : fMVAPdfB);

   return thePdf->GetIntegral( thePdf->GetXmin(), mvaVal );
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetEfficiency( const TString& theString, Types::ETreeType type,Double_t& effSerr )
{
   // fill background efficiency (resp. rejection) versus signal efficiency plots
   // returns signal efficiency at background efficiency indicated in theString

   Data()->SetCurrentType(type);
   Results* results = Data()->GetResults( GetMethodName(), type, Types::kClassification );
   std::vector<Float_t>* mvaRes = dynamic_cast<ResultsClassification*>(results)->GetValueVector();

   // parse input string for required background efficiency
   TList* list  = gTools().ParseFormatLine( theString );

   // sanity check
   Bool_t computeArea = kFALSE;
   if      (!list || list->GetSize() < 2) computeArea = kTRUE; // the area is computed
   else if (list->GetSize() > 2) {
      Log() << kFATAL << "<GetEfficiency> Wrong number of arguments"
            << " in string: " << theString
            << " | required format, e.g., Efficiency:0.05, or empty string" << Endl;
      delete list;
      return -1;
   }

   // sanity check
   if ( results->GetHist("MVA_S")->GetNbinsX() != results->GetHist("MVA_B")->GetNbinsX() ||
        results->GetHist("MVA_HIGHBIN_S")->GetNbinsX() != results->GetHist("MVA_HIGHBIN_B")->GetNbinsX() ) {
      Log() << kFATAL << "<GetEfficiency> Binning mismatch between signal and background histos" << Endl;
      delete list;
      return -1.0;
   }

   // create histograms

   // first, get efficiency histograms for signal and background
   TH1 * effhist = results->GetHist("MVA_HIGHBIN_S");
   Double_t xmin = effhist->GetXaxis()->GetXmin();
   Double_t xmax = effhist->GetXaxis()->GetXmax();

   static Double_t nevtS;

   // first round ? --> create histograms
   if (results->GetHist("MVA_EFF_S")==0) {

      // for efficiency plot
      TH1* eff_s = new TH1F( GetTestvarName() + "_effS", GetTestvarName() + " (signal)",     fNbinsH, xmin, xmax );
      TH1* eff_b = new TH1F( GetTestvarName() + "_effB", GetTestvarName() + " (background)", fNbinsH, xmin, xmax );
      results->Store(eff_s, "MVA_EFF_S");
      results->Store(eff_b, "MVA_EFF_B");

      // sign if cut
      Int_t sign = (fCutOrientation == kPositive) ? +1 : -1;

      // this method is unbinned
      nevtS = 0;
      for (UInt_t ievt=0; ievt<Data()->GetNEvents(); ievt++) {

         // read the tree
         Bool_t  isSignal  = DataInfo().IsSignal(GetEvent(ievt));
         Float_t theWeight = GetEvent(ievt)->GetWeight();
         Float_t theVal    = (*mvaRes)[ievt];

         // select histogram depending on if sig or bgd
         TH1* theHist = isSignal ? eff_s : eff_b;

         // count signal and background events in tree
         if (isSignal) nevtS+=theWeight;

         TAxis* axis   = theHist->GetXaxis();
         Int_t  maxbin = Int_t((theVal - axis->GetXmin())/(axis->GetXmax() - axis->GetXmin())*fNbinsH) + 1;
         if (sign > 0 && maxbin > fNbinsH) continue; // can happen... event doesn't count
         if (sign < 0 && maxbin < 1      ) continue; // can happen... event doesn't count
         if (sign > 0 && maxbin < 1      ) maxbin = 1;
         if (sign < 0 && maxbin > fNbinsH) maxbin = fNbinsH;

         if (sign > 0)
            for (Int_t ibin=1; ibin<=maxbin; ibin++) theHist->AddBinContent( ibin , theWeight);
         else if (sign < 0)
            for (Int_t ibin=maxbin+1; ibin<=fNbinsH; ibin++) theHist->AddBinContent( ibin , theWeight );
         else
            Log() << kFATAL << "<GetEfficiency> Mismatch in sign" << Endl;
      }

      // renormalise maximum to <=1
      eff_s->Scale( 1.0/TMath::Max(1.,eff_s->GetMaximum()) );
      eff_b->Scale( 1.0/TMath::Max(1.,eff_b->GetMaximum()) );

      // background efficiency versus signal efficiency
      TH1* eff_BvsS = new TH1F( GetTestvarName() + "_effBvsS", GetTestvarName() + "", fNbins, 0, 1 );
      results->Store(eff_BvsS, "MVA_EFF_BvsS");
      eff_BvsS->SetXTitle( "Signal eff" );
      eff_BvsS->SetYTitle( "Backgr eff" );

      // background rejection (=1-eff.) versus signal efficiency
      TH1* rej_BvsS = new TH1F( GetTestvarName() + "_rejBvsS", GetTestvarName() + "", fNbins, 0, 1 );
      results->Store(rej_BvsS);
      rej_BvsS->SetXTitle( "Signal eff" );
      rej_BvsS->SetYTitle( "Backgr rejection (1-eff)" );

      // inverse background eff (1/eff.) versus signal efficiency
      TH1* inveff_BvsS = new TH1F( GetTestvarName() + "_invBeffvsSeff",
                                   GetTestvarName(), fNbins, 0, 1 );
      results->Store(inveff_BvsS);
      inveff_BvsS->SetXTitle( "Signal eff" );
      inveff_BvsS->SetYTitle( "Inverse backgr. eff (1/eff)" );

      // use root finder
      // spline background efficiency plot
      // note that there is a bin shift when going from a TH1F object to a TGraph :-(
      if (Use_Splines_for_Eff_) {
         fSplRefS  = new TSpline1( "spline2_signal",     new TGraph( eff_s ) );
         fSplRefB  = new TSpline1( "spline2_background", new TGraph( eff_b ) );

         // verify spline sanity
         gTools().CheckSplines( eff_s, fSplRefS );
         gTools().CheckSplines( eff_b, fSplRefB );
      }

      // make the background-vs-signal efficiency plot

      // create root finder
      // reset static "this" pointer before calling external function
      ResetThisBase();
      RootFinder rootFinder( &IGetEffForRoot, fXmin, fXmax );

      Double_t effB = 0;
      fEffS = eff_s; // to be set for the root finder
      for (Int_t bini=1; bini<=fNbins; bini++) {

         // find cut value corresponding to a given signal efficiency
         Double_t effS = eff_BvsS->GetBinCenter( bini );
         Double_t cut  = rootFinder.Root( effS );

         // retrieve background efficiency for given cut
         if (Use_Splines_for_Eff_) effB = fSplRefB->Eval( cut );
         else                      effB = eff_b->GetBinContent( eff_b->FindBin( cut ) );

         // and fill histograms
         eff_BvsS->SetBinContent( bini, effB     );
         rej_BvsS->SetBinContent( bini, 1.0-effB );
         if (effB>std::numeric_limits<double>::epsilon())
            inveff_BvsS->SetBinContent( bini, 1.0/effB );
      }

      // create splines for histogram
      fSpleffBvsS = new TSpline1( "effBvsS", new TGraph( eff_BvsS ) );

      // search for overlap point where, when cutting on it,
      // one would obtain: eff_S = rej_B = 1 - eff_B
      Double_t effS, rejB, effS_ = 0, rejB_ = 0;
      Int_t    nbins_ = 5000;
      for (Int_t bini=1; bini<=nbins_; bini++) {

         // get corresponding signal and background efficiencies
         effS = (bini - 0.5)/Float_t(nbins_);
         rejB = 1.0 - fSpleffBvsS->Eval( effS );

         // find signal efficiency that corresponds to required background efficiency
         if ((effS - rejB)*(effS_ - rejB_) < 0) break;
         effS_ = effS;
         rejB_ = rejB;
      }

      // find cut that corresponds to signal efficiency and update signal-like criterion
      Double_t cut = rootFinder.Root( 0.5*(effS + effS_) );
      SetSignalReferenceCut( cut );
      fEffS = 0;
   }

   // must exist...
   if (0 == fSpleffBvsS) {
      delete list;
      return 0.0;
   }

   // now find signal efficiency that corresponds to required background efficiency
   Double_t effS = 0, effB = 0, effS_ = 0, effB_ = 0;
   Int_t    nbins_ = 1000;

   if (computeArea) {

      // compute area of rej-vs-eff plot
      Double_t integral = 0;
      for (Int_t bini=1; bini<=nbins_; bini++) {

         // get corresponding signal and background efficiencies
         effS = (bini - 0.5)/Float_t(nbins_);
         effB = fSpleffBvsS->Eval( effS );
         integral += (1.0 - effB);
      }
      integral /= nbins_;

      delete list;
      return integral;
   }
   else {

      // that will be the value of the efficiency retured (does not affect
      // the efficiency-vs-bkg plot which is done anyway.
      Float_t effBref  = atof( ((TObjString*)list->At(1))->GetString() );

      // find precise efficiency value
      for (Int_t bini=1; bini<=nbins_; bini++) {

         // get corresponding signal and background efficiencies
         effS = (bini - 0.5)/Float_t(nbins_);
         effB = fSpleffBvsS->Eval( effS );

         // find signal efficiency that corresponds to required background efficiency
         if ((effB - effBref)*(effB_ - effBref) <= 0) break;
         effS_ = effS;
         effB_ = effB;
      }

      // take mean between bin above and bin below
      effS = 0.5*(effS + effS_);

      effSerr = 0;
      if (nevtS > 0) effSerr = TMath::Sqrt( effS*(1.0 - effS)/nevtS );

      delete list;
      return effS;
   }

   return -1;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetTrainingEfficiency(const TString& theString)
{
   Data()->SetCurrentType(Types::kTraining);

   Results* results = Data()->GetResults(GetMethodName(), Types::kTesting, Types::kNoAnalysisType);

   // fill background efficiency (resp. rejection) versus signal efficiency plots
   // returns signal efficiency at background efficiency indicated in theString

   // parse input string for required background efficiency
   TList*  list  = gTools().ParseFormatLine( theString );
   // sanity check

   if (list->GetSize() != 2) {
      Log() << kFATAL << "<GetTrainingEfficiency> Wrong number of arguments"
            << " in string: " << theString
            << " | required format, e.g., Efficiency:0.05" << Endl;
      delete list;
      return -1;
   }
   // that will be the value of the efficiency retured (does not affect
   // the efficiency-vs-bkg plot which is done anyway.
   Float_t effBref  = atof( ((TObjString*)list->At(1))->GetString() );

   delete list;

   // sanity check
   if (results->GetHist("MVA_S")->GetNbinsX() != results->GetHist("MVA_B")->GetNbinsX() ||
       results->GetHist("MVA_HIGHBIN_S")->GetNbinsX() != results->GetHist("MVA_HIGHBIN_B")->GetNbinsX() ) {
      Log() << kFATAL << "<GetTrainingEfficiency> Binning mismatch between signal and background histos"
            << Endl;
      return -1.0;
   }

   // create histogram

   // first, get efficiency histograms for signal and background
   TH1 * effhist = results->GetHist("MVA_HIGHBIN_S");
   Double_t xmin = effhist->GetXaxis()->GetXmin();
   Double_t xmax = effhist->GetXaxis()->GetXmax();

   // first round ? --> create and fill histograms
   if (results->GetHist("MVA_TRAIN_S")==0) {

      // classifier response distributions for test sample
      Double_t sxmax = fXmax+0.00001;

      // MVA plots on the training sample (check for overtraining)
      TH1* mva_s_tr = new TH1F( GetTestvarName() + "_Train_S",GetTestvarName() + "_Train_S", fNbins, fXmin, sxmax );
      TH1* mva_b_tr = new TH1F( GetTestvarName() + "_Train_B",GetTestvarName() + "_Train_B", fNbins, fXmin, sxmax );
      results->Store(mva_s_tr, "MVA_TRAIN_S");
      results->Store(mva_b_tr, "MVA_TRAIN_B");
      mva_s_tr->Sumw2();
      mva_b_tr->Sumw2();

      // Training efficiency plots
      TH1* mva_eff_tr_s = new TH1F( GetTestvarName() + "_trainingEffS", GetTestvarName() + " (signal)",
                                    fNbinsH, xmin, xmax );
      TH1* mva_eff_tr_b = new TH1F( GetTestvarName() + "_trainingEffB", GetTestvarName() + " (background)",
                                    fNbinsH, xmin, xmax );
      results->Store(mva_eff_tr_s, "MVA_TRAINEFF_S");
      results->Store(mva_eff_tr_b, "MVA_TRAINEFF_B");

      // sign if cut
      Int_t sign = (fCutOrientation == kPositive) ? +1 : -1;

      // this method is unbinned
      for (Int_t ievt=0; ievt<Data()->GetNEvents(); ievt++) {

         Data()->SetCurrentEvent(ievt);
         const Event* ev = GetEvent();

         Double_t theVal    = GetMvaValue();
         Double_t theWeight = ev->GetWeight();

         TH1* theEffHist = DataInfo().IsSignal(ev) ? mva_eff_tr_s : mva_eff_tr_b;
         TH1* theClsHist = DataInfo().IsSignal(ev) ? mva_s_tr : mva_b_tr;

         theClsHist->Fill( theVal, theWeight );

         TAxis* axis   = theEffHist->GetXaxis();
         Int_t  maxbin = Int_t((theVal - axis->GetXmin())/(axis->GetXmax() - axis->GetXmin())*fNbinsH) + 1;
         if (sign > 0 && maxbin > fNbinsH) continue; // can happen... event doesn't count
         if (sign < 0 && maxbin < 1      ) continue; // can happen... event doesn't count
         if (sign > 0 && maxbin < 1      ) maxbin = 1;
         if (sign < 0 && maxbin > fNbinsH) maxbin = fNbinsH;

         if (sign > 0)
            for (Int_t ibin=1; ibin<=maxbin; ibin++) theEffHist->AddBinContent( ibin , theWeight );
         else 
            for (Int_t ibin=maxbin+1; ibin<=fNbinsH; ibin++) theEffHist->AddBinContent( ibin , theWeight );
      }

      // normalise output distributions
      gTools().NormHist( mva_s_tr  );
      gTools().NormHist( mva_b_tr  );

      // renormalise to maximum
      mva_eff_tr_s->Scale( 1.0/TMath::Max(1.0, mva_eff_tr_s->GetMaximum()) );
      mva_eff_tr_b->Scale( 1.0/TMath::Max(1.0, mva_eff_tr_b->GetMaximum()) );

      // Training background efficiency versus signal efficiency
      TH1* eff_bvss = new TH1F( GetTestvarName() + "_trainingEffBvsS", GetTestvarName() + "", fNbins, 0, 1 );
      // Training background rejection (=1-eff.) versus signal efficiency
      TH1* rej_bvss = new TH1F( GetTestvarName() + "_trainingRejBvsS", GetTestvarName() + "", fNbins, 0, 1 );
      results->Store(eff_bvss, "EFF_BVSS_TR");
      results->Store(rej_bvss, "REJ_BVSS_TR");

      // use root finder
      // spline background efficiency plot
      // note that there is a bin shift when going from a TH1F object to a TGraph :-(
      if (Use_Splines_for_Eff_) {
         if (fSplTrainRefS) delete fSplTrainRefS;
         if (fSplTrainRefB) delete fSplTrainRefB;         
         fSplTrainRefS  = new TSpline1( "spline2_signal",     new TGraph( mva_eff_tr_s ) );
         fSplTrainRefB  = new TSpline1( "spline2_background", new TGraph( mva_eff_tr_b ) );

         // verify spline sanity
         gTools().CheckSplines( mva_eff_tr_s, fSplTrainRefS );
         gTools().CheckSplines( mva_eff_tr_b, fSplTrainRefB );
      }

      // make the background-vs-signal efficiency plot

      // create root finder
      // reset static "this" pointer before calling external function
      ResetThisBase();
      RootFinder rootFinder(&IGetEffForRoot, fXmin, fXmax );

      Double_t effB = 0;
      fEffS = results->GetHist("MVA_TRAINEFF_S");
      for (Int_t bini=1; bini<=fNbins; bini++) {

         // find cut value corresponding to a given signal efficiency
         Double_t effS = eff_bvss->GetBinCenter( bini );

         Double_t cut  = rootFinder.Root( effS );

         // retrieve background efficiency for given cut
         if (Use_Splines_for_Eff_) effB = fSplTrainRefB->Eval( cut );
         else                      effB = mva_eff_tr_b->GetBinContent( mva_eff_tr_b->FindBin( cut ) );

         // and fill histograms
         eff_bvss->SetBinContent( bini, effB     );
         rej_bvss->SetBinContent( bini, 1.0-effB );
      }
      fEffS = 0;

      // create splines for histogram
      fSplTrainEffBvsS = new TSpline1( "effBvsS", new TGraph( eff_bvss ) );
   }

   // must exist...
   if (0 == fSplTrainEffBvsS) return 0.0;

   // now find signal efficiency that corresponds to required background efficiency
   Double_t effS, effB, effS_ = 0, effB_ = 0;
   Int_t    nbins_ = 1000;
   for (Int_t bini=1; bini<=nbins_; bini++) {

      // get corresponding signal and background efficiencies
      effS = (bini - 0.5)/Float_t(nbins_);
      effB = fSplTrainEffBvsS->Eval( effS );

      // find signal efficiency that corresponds to required background efficiency
      if ((effB - effBref)*(effB_ - effBref) <= 0) break;
      effS_ = effS;
      effB_ = effB;
   }

   return 0.5*(effS + effS_); // the mean between bin above and bin below
}

//_______________________________________________________________________


std::vector<Float_t> TMVA::MethodBase::GetMulticlassEfficiency(std::vector<std::vector<Float_t> >& purity)
{
   Data()->SetCurrentType(Types::kTesting);
   ResultsMulticlass* resMulticlass = dynamic_cast<ResultsMulticlass*>(Data()->GetResults(GetMethodName(), Types::kTesting, Types::kMulticlass));
   if (!resMulticlass) Log() << kFATAL<< "unable to create pointer in GetMulticlassEfficiency, exiting."<<Endl;

   purity.push_back(resMulticlass->GetAchievablePur()); 
   return resMulticlass->GetAchievableEff(); 
}

//_______________________________________________________________________

std::vector<Float_t> TMVA::MethodBase::GetMulticlassTrainingEfficiency(std::vector<std::vector<Float_t> >& purity)
{
   Data()->SetCurrentType(Types::kTraining);
   ResultsMulticlass* resMulticlass = dynamic_cast<ResultsMulticlass*>(Data()->GetResults(GetMethodName(), Types::kTraining, Types::kMulticlass));
   if (!resMulticlass) Log() << kFATAL<< "unable to create pointer in GetMulticlassTrainingEfficiency, exiting."<<Endl;
   
   Log() << kINFO << "Determine optimal multiclass cuts for training data..." << Endl;
   for(UInt_t icls = 0; icls<DataInfo().GetNClasses(); ++icls){
      resMulticlass->GetBestMultiClassCuts(icls);
   }
    
   purity.push_back(resMulticlass->GetAchievablePur()); 
   return resMulticlass->GetAchievableEff(); 
}


//_______________________________________________________________________
Double_t TMVA::MethodBase::GetSignificance( void ) const
{
   // compute significance of mean difference
   // significance = |<S> - <B>|/Sqrt(RMS_S2 + RMS_B2)
   Double_t rms = sqrt( fRmsS*fRmsS + fRmsB*fRmsB );

   return (rms > 0) ? TMath::Abs(fMeanS - fMeanB)/rms : 0;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetSeparation( TH1* histoS, TH1* histoB ) const
{
   // compute "separation" defined as
   // <s2> = (1/2) Int_-oo..+oo { (S(x) - B(x))^2/(S(x) + B(x)) dx }
   return gTools().GetSeparation( histoS, histoB );
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetSeparation( PDF* pdfS, PDF* pdfB ) const
{
   // compute "separation" defined as
   // <s2> = (1/2) Int_-oo..+oo { (S(x)2 - B(x)2)/(S(x) + B(x)) dx }

   // note, if zero pointers given, use internal pdf
   // sanity check first
   if ((!pdfS && pdfB) || (pdfS && !pdfB))
      Log() << kFATAL << "<GetSeparation> Mismatch in pdfs" << Endl;
   if (!pdfS) pdfS = fSplS;
   if (!pdfB) pdfB = fSplB;

   if (!fSplS || !fSplB){
      Log()<<kWARNING<< "could not calculate the separation, distributions"
           << " fSplS or fSplB are not yet filled" << Endl;
      return 0;
   }else{
      return gTools().GetSeparation( *pdfS, *pdfB );
   }
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetROCIntegral(PDF *pdfS, PDF *pdfB) const
{
   // calculate the area (integral) under the ROC curve as a
   // overall quality measure of the classification

   // note, if zero pointers given, use internal pdf
   // sanity check first
   if ((!pdfS && pdfB) || (pdfS && !pdfB))
      Log() << kFATAL << "<GetSeparation> Mismatch in pdfs" << Endl;
   if (!pdfS) pdfS = fSplS;
   if (!pdfB) pdfB = fSplB;

   if(pdfS==0 || pdfB==0) return 0.;

   Double_t xmin = TMath::Min(pdfS->GetXmin(), pdfB->GetXmin());
   Double_t xmax = TMath::Max(pdfS->GetXmax(), pdfB->GetXmax());

   Double_t integral = 0;
   UInt_t   nsteps = 1000;
   Double_t step = (xmax-xmin)/Double_t(nsteps);
   Double_t cut = xmin;
   for (UInt_t i=0; i<nsteps; i++){
      integral += (1-pdfB->GetIntegral(cut,xmax)) * pdfS->GetVal(cut);
      cut+=step;
   } 
   return integral*step;
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetMaximumSignificance( Double_t SignalEvents,
                                                   Double_t BackgroundEvents,
                                                   Double_t& max_significance_value ) const
{
   // plot significance, S/Sqrt(S^2 + B^2), curve for given number
   // of signal and background events; returns cut for maximum significance
   // also returned via reference is the maximum significance

   Results* results = Data()->GetResults( GetMethodName(), Types::kTesting, Types::kMaxAnalysisType );

   Double_t max_significance(0);
   Double_t effS(0),effB(0),significance(0);
   TH1F *temp_histogram = new TH1F("temp", "temp", fNbinsH, fXmin, fXmax );

   if (SignalEvents <= 0 || BackgroundEvents <= 0) {
      Log() << kFATAL << "<GetMaximumSignificance> "
            << "Number of signal or background events is <= 0 ==> abort"
            << Endl;
   }

   Log() << kINFO << "Using ratio SignalEvents/BackgroundEvents = "
         << SignalEvents/BackgroundEvents << Endl;

   TH1* eff_s = results->GetHist("MVA_EFF_S");
   TH1* eff_b = results->GetHist("MVA_EFF_B");

   if ( (eff_s==0) || (eff_b==0) ) {
      Log() << kWARNING << "Efficiency histograms empty !" << Endl;
      Log() << kWARNING << "no maximum cut found, return 0" << Endl;
      return 0;
   }

   for (Int_t bin=1; bin<=fNbinsH; bin++) {
      effS = eff_s->GetBinContent( bin );
      effB = eff_b->GetBinContent( bin );

      // put significance into a histogram
      significance = sqrt(SignalEvents)*( effS )/sqrt( effS + ( BackgroundEvents / SignalEvents) * effB  );

      temp_histogram->SetBinContent(bin,significance);
   }

   // find maximum in histogram
   max_significance = temp_histogram->GetBinCenter( temp_histogram->GetMaximumBin() );
   max_significance_value = temp_histogram->GetBinContent( temp_histogram->GetMaximumBin() );

   // delete
   delete temp_histogram;

   Log() << kINFO << "Optimal cut at      : " << max_significance << Endl;
   Log() << kINFO << "Maximum significance: " << max_significance_value << Endl;

   return max_significance;
}

//_______________________________________________________________________
void TMVA::MethodBase::Statistics( Types::ETreeType treeType, const TString& theVarName,
                                   Double_t& meanS, Double_t& meanB,
                                   Double_t& rmsS,  Double_t& rmsB,
                                   Double_t& xmin,  Double_t& xmax )
{
   // calculates rms,mean, xmin, xmax of the event variable
   // this can be either done for the variables as they are or for
   // normalised variables (in the range of 0-1) if "norm" is set to kTRUE

   Types::ETreeType previousTreeType = Data()->GetCurrentType();
   Data()->SetCurrentType(treeType);

   Long64_t entries = Data()->GetNEvents();

   // sanity check
   if (entries <=0)
      Log() << kFATAL << "<CalculateEstimator> Wrong tree type: " << treeType << Endl;

   // index of the wanted variable
   UInt_t varIndex = DataInfo().FindVarIndex( theVarName );

   // first fill signal and background in arrays before analysis
   xmin               = +DBL_MAX;
   xmax               = -DBL_MAX;
   Long64_t nEventsS  = -1;
   Long64_t nEventsB  = -1;

   // take into account event weights
   meanS = 0;
   meanB = 0;
   rmsS  = 0;
   rmsB  = 0;
   Double_t sumwS = 0, sumwB = 0;

   // loop over all training events
   for (Int_t ievt = 0; ievt < entries; ievt++) {

      const Event* ev = GetEvent(ievt);

      Double_t theVar = ev->GetValue(varIndex);
      Double_t weight = ev->GetWeight();

      if (DataInfo().IsSignal(ev)) {
         sumwS               += weight;
         meanS               += weight*theVar;
         rmsS                += weight*theVar*theVar;
      }
      else {
         sumwB               += weight;
         meanB               += weight*theVar;
         rmsB                += weight*theVar*theVar;
      }
      xmin = TMath::Min( xmin, theVar );
      xmax = TMath::Max( xmax, theVar );
   }
   ++nEventsS;
   ++nEventsB;

   meanS = meanS/sumwS;
   meanB = meanB/sumwB;
   rmsS  = TMath::Sqrt( rmsS/sumwS - meanS*meanS );
   rmsB  = TMath::Sqrt( rmsB/sumwB - meanB*meanB );

   Data()->SetCurrentType(previousTreeType);
}

//_______________________________________________________________________
void TMVA::MethodBase::MakeClass( const TString& theClassFileName ) const
{
   // create reader class for method (classification only at present)

   // the default consists of
   TString classFileName = "";
   if (theClassFileName == "")
      classFileName = GetWeightFileDir() + "/" + GetJobName() + "_" + GetMethodName() + ".class.C";
   else
      classFileName = theClassFileName;

   TString className = TString("Read") + GetMethodName();

   TString tfname( classFileName );
   Log() << kINFO << "Creating standalone response class: "
         << gTools().Color("lightblue") << classFileName << gTools().Color("reset") << Endl;

   ofstream fout( classFileName );
   if (!fout.good()) { // file could not be opened --> Error
      Log() << kFATAL << "<MakeClass> Unable to open file: " << classFileName << Endl;
   }

   // now create the class
   // preamble
   fout << "// Class: " << className << endl;
   fout << "// Automatically generated by MethodBase::MakeClass" << endl << "//" << endl;

   // print general information and configuration state
   fout << endl;
   fout << "/* configuration options =====================================================" << endl << endl;
   WriteStateToStream( fout );
   fout << endl;
   fout << "============================================================================ */" << endl;

   // generate the class
   fout << "" << endl;
   fout << "#include <vector>" << endl;
   fout << "#include <cmath>" << endl;
   fout << "#include <string>" << endl;
   fout << "#include <iostream>" << endl;
   fout << "" << endl;
   // now if the classifier needs to write some addicional classes for its response implementation
   // this code goes here: (at least the header declarations need to come before the main class
   this->MakeClassSpecificHeader( fout, className );

   fout << "#ifndef IClassifierReader__def" << endl;
   fout << "#define IClassifierReader__def" << endl;
   fout << endl;
   fout << "class IClassifierReader {" << endl;
   fout << endl;
   fout << " public:" << endl;
   fout << endl;
   fout << "   // constructor" << endl;
   fout << "   IClassifierReader() : fStatusIsClean( true ) {}" << endl;
   fout << "   virtual ~IClassifierReader() {}" << endl;
   fout << endl;
   fout << "   // return classifier response" << endl;
   fout << "   virtual double GetMvaValue( const std::vector<double>& inputValues ) const = 0;" << endl;
   fout << endl;
   fout << "   // returns classifier status" << endl;
   fout << "   Bool_t IsStatusClean() const { return fStatusIsClean; }" << endl;
   fout << endl;
   fout << " protected:" << endl;
   fout << endl;
   fout << "   Bool_t fStatusIsClean;" << endl;
   fout << "};" << endl;
   fout << endl;
   fout << "#endif" << endl;
   fout << endl;
   fout << "class " << className << " : public IClassifierReader {" << endl;
   fout << endl;
   fout << " public:" << endl;
   fout << endl;
   fout << "   // constructor" << endl;
   fout << "   " << className << "( std::vector<std::string>& theInputVars ) " << endl;
   fout << "      : IClassifierReader()," << endl;
   fout << "        fClassName( \"" << className << "\" )," << endl;
   fout << "        fNvars( " << GetNvar() << " )," << endl;
   fout << "        fIsNormalised( " << (IsNormalised() ? "true" : "false") << " )" << endl;
   fout << "   {      " << endl;
   fout << "      // the training input variables" << endl;
   fout << "      const char* inputVars[] = { ";
   for (UInt_t ivar=0; ivar<GetNvar(); ivar++) {
      fout << "\"" << GetOriginalVarName(ivar) << "\"";
      if (ivar<GetNvar()-1) fout << ", ";
   }
   fout << " };" << endl;
   fout << endl;
   fout << "      // sanity checks" << endl;
   fout << "      if (theInputVars.size() <= 0) {" << endl;
   fout << "         std::cout << \"Problem in class \\\"\" << fClassName << \"\\\": empty input vector\" << std::endl;" << endl;
   fout << "         fStatusIsClean = false;" << endl;
   fout << "      }" << endl;
   fout << endl;
   fout << "      if (theInputVars.size() != fNvars) {" << endl;
   fout << "         std::cout << \"Problem in class \\\"\" << fClassName << \"\\\": mismatch in number of input values: \"" << endl;
   fout << "                   << theInputVars.size() << \" != \" << fNvars << std::endl;" << endl;
   fout << "         fStatusIsClean = false;" << endl;
   fout << "      }" << endl;
   fout << endl;
   fout << "      // validate input variables" << endl;
   fout << "      for (size_t ivar = 0; ivar < theInputVars.size(); ivar++) {" << endl;
   fout << "         if (theInputVars[ivar] != inputVars[ivar]) {" << endl;
   fout << "            std::cout << \"Problem in class \\\"\" << fClassName << \"\\\": mismatch in input variable names\" << std::endl" << endl;
   fout << "                      << \" for variable [\" << ivar << \"]: \" << theInputVars[ivar].c_str() << \" != \" << inputVars[ivar] << std::endl;" << endl;
   fout << "            fStatusIsClean = false;" << endl;
   fout << "         }" << endl;
   fout << "      }" << endl;
   fout << endl;
   fout << "      // initialize min and max vectors (for normalisation)" << endl;
   for (UInt_t ivar = 0; ivar < GetNvar(); ivar++) {
      fout << "      fVmin[" << ivar << "] = " << std::setprecision(15) << GetXmin( ivar ) << ";" << endl;
      fout << "      fVmax[" << ivar << "] = " << std::setprecision(15) << GetXmax( ivar ) << ";" << endl;
   }
   fout << endl;
   fout << "      // initialize input variable types" << endl;
   for (UInt_t ivar=0; ivar<GetNvar(); ivar++) {
      fout << "      fType[" << ivar << "] = \'" << DataInfo().GetVariableInfo(ivar).GetVarType() << "\';" << endl;
   }
   fout << endl;
   fout << "      // initialize constants" << endl;
   fout << "      Initialize();" << endl;
   fout << endl;
   if (GetTransformationHandler().GetTransformationList().GetSize() != 0) {
      fout << "      // initialize transformation" << endl;
      fout << "      InitTransform();" << endl;
   }
   fout << "   }" << endl;
   fout << endl;
   fout << "   // destructor" << endl;
   fout << "   virtual ~" << className << "() {" << endl;
   fout << "      Clear(); // method-specific" << endl;
   fout << "   }" << endl;
   fout << endl;
   fout << "   // the classifier response" << endl;
   fout << "   // \"inputValues\" is a vector of input values in the same order as the " << endl;
   fout << "   // variables given to the constructor" << endl;
   fout << "   double GetMvaValue( const std::vector<double>& inputValues ) const;" << endl;
   fout << endl;
   fout << " private:" << endl;
   fout << endl;
   fout << "   // method-specific destructor" << endl;
   fout << "   void Clear();" << endl;
   fout << endl;
   if (GetTransformationHandler().GetTransformationList().GetSize()!=0) {
      fout << "   // input variable transformation" << endl;
      GetTransformationHandler().MakeFunction(fout, className,1);
      fout << "   void InitTransform();" << endl;
      fout << "   void Transform( std::vector<double> & iv, int sigOrBgd ) const;" << endl;
      fout << endl;
   }
   fout << "   // common member variables" << endl;
   fout << "   const char* fClassName;" << endl;
   fout << endl;
   fout << "   const size_t fNvars;" << endl;
   fout << "   size_t GetNvar()           const { return fNvars; }" << endl;
   fout << "   char   GetType( int ivar ) const { return fType[ivar]; }" << endl;
   fout << endl;
   fout << "   // normalisation of input variables" << endl;
   fout << "   const Bool_t fIsNormalised;" << endl;
   fout << "   Bool_t IsNormalised() const { return fIsNormalised; }" << endl;
   fout << "   double fVmin[" << GetNvar() << "];" << endl;
   fout << "   double fVmax[" << GetNvar() << "];" << endl;
   fout << "   double NormVariable( double x, double xmin, double xmax ) const {" << endl;
   fout << "      // normalise to output range: [-1, 1]" << endl;
   fout << "      return 2*(x - xmin)/(xmax - xmin) - 1.0;" << endl;
   fout << "   }" << endl;
   fout << endl;
   fout << "   // type of input variable: 'F' or 'I'" << endl;
   fout << "   char   fType[" << GetNvar() << "];" << endl;
   fout << endl;
   fout << "   // initialize internal variables" << endl;
   fout << "   void Initialize();" << endl;
   fout << "   double GetMvaValue__( const std::vector<double>& inputValues ) const;" << endl;
   fout << "" << endl;
   fout << "   // private members (method specific)" << endl;

   // call the classifier specific output (the classifier must close the class !)
   MakeClassSpecific( fout, className );

   fout << "   inline double " << className << "::GetMvaValue( const std::vector<double>& inputValues ) const" << endl;
   fout << "   {" << endl;
   fout << "      // classifier response value" << endl;
   fout << "      double retval = 0;" << endl;
   fout << endl;
   fout << "      // classifier response, sanity check first" << endl;
   fout << "      if (!IsStatusClean()) {" << endl;
   fout << "         std::cout << \"Problem in class \\\"\" << fClassName << \"\\\": cannot return classifier response\"" << endl;
   fout << "                   << \" because status is dirty\" << std::endl;" << endl;
   fout << "         retval = 0;" << endl;
   fout << "      }" << endl;
   fout << "      else {" << endl;
   fout << "         if (IsNormalised()) {" << endl;
   fout << "            // normalise variables" << endl;
   fout << "            std::vector<double> iV;" << endl;
   fout << "            int ivar = 0;" << endl;
   fout << "            for (std::vector<double>::const_iterator varIt = inputValues.begin();" << endl;
   fout << "                 varIt != inputValues.end(); varIt++, ivar++) {" << endl;
   fout << "               iV.push_back(NormVariable( *varIt, fVmin[ivar], fVmax[ivar] ));" << endl;
   fout << "            }" << endl;
   if (GetTransformationHandler().GetTransformationList().GetSize()!=0 && GetMethodType() != Types::kLikelihood)
      fout << "            Transform( iV, -1 );" << endl;
   fout << "            retval = GetMvaValue__( iV );" << endl;
   fout << "         }" << endl;
   fout << "         else {" << endl;
   if (GetTransformationHandler().GetTransformationList().GetSize()!=0 && GetMethodType() != Types::kLikelihood) {
      fout << "            std::vector<double> iV;" << endl;
      fout << "            int ivar = 0;" << endl;
      fout << "            for (std::vector<double>::const_iterator varIt = inputValues.begin();" << endl;
      fout << "                 varIt != inputValues.end(); varIt++, ivar++) {" << endl;
      fout << "               iV.push_back(*varIt);" << endl;
      fout << "            }" << endl;
      fout << "            Transform( iV, -1 );" << endl;
      fout << "            retval = GetMvaValue__( iV );" << endl;
   }
   else {
      fout << "            retval = GetMvaValue__( inputValues );" << endl;
   }
   fout << "         }" << endl;
   fout << "      }" << endl;
   fout << endl;
   fout << "      return retval;" << endl;
   fout << "   }" << endl;

   // create output for transformation - if any
   if (GetTransformationHandler().GetTransformationList().GetSize()!=0)
      GetTransformationHandler().MakeFunction(fout, className,2);

   // close the file
   fout.close();
}

//_______________________________________________________________________
void TMVA::MethodBase::PrintHelpMessage() const
{
   // prints out method-specific help method

   // if options are written to reference file, also append help info
   std::streambuf* cout_sbuf = std::cout.rdbuf(); // save original sbuf
   std::ofstream* o = 0;
   if (gConfig().WriteOptionsReference()) {
      Log() << kINFO << "Print Help message for class " << GetName() << " into file: " << GetReferenceFile() << Endl;
      o = new std::ofstream( GetReferenceFile(), std::ios::app );
      if (!o->good()) { // file could not be opened --> Error
         Log() << kFATAL << "<PrintHelpMessage> Unable to append to output file: " << GetReferenceFile() << Endl;
      }
      std::cout.rdbuf( o->rdbuf() ); // redirect 'cout' to file
   }

   //         "|--------------------------------------------------------------|"
   if (!o) {
      Log() << kINFO << Endl;
      Log() << gTools().Color("bold")
            << "================================================================"
            << gTools().Color( "reset" )
            << Endl;
      Log() << gTools().Color("bold")
            << "H e l p   f o r   M V A   m e t h o d   [ " << GetName() << " ] :"
            << gTools().Color( "reset" )
            << Endl;
   }
   else {
      Log() << "Help for MVA method [ " << GetName() << " ] :" << Endl;
   }

   // print method-specific help message
   GetHelpMessage();

   if (!o) {
      Log() << Endl;
      Log() << "<Suppress this message by specifying \"!H\" in the booking option>" << Endl;
      Log() << gTools().Color("bold")
            << "================================================================"
            << gTools().Color( "reset" )
            << Endl;
      Log() << Endl;
   }
   else {
      // indicate END
      Log() << "# End of Message___" << Endl;
   }

   std::cout.rdbuf( cout_sbuf ); // restore the original stream buffer
   if (o) o->close();
}

// ----------------------- r o o t   f i n d i n g ----------------------------

TMVA::MethodBase* TMVA::MethodBase::fgThisBase = 0;

//_______________________________________________________________________
Double_t TMVA::MethodBase::IGetEffForRoot( Double_t theCut )
{
   // interface for RootFinder
   return MethodBase::GetThisBase()->GetEffForRoot( theCut );
}

//_______________________________________________________________________
Double_t TMVA::MethodBase::GetEffForRoot( Double_t theCut )
{
   // returns efficiency as function of cut
   Double_t retval=0;

   // retrieve the class object
   if (Use_Splines_for_Eff_) {
      retval = fSplRefS->Eval( theCut );
   }
   else retval = fEffS->GetBinContent( fEffS->FindBin( theCut ) );

   // caution: here we take some "forbidden" action to hide a problem:
   // in some cases, in particular for likelihood, the binned efficiency distributions
   // do not equal 1, at xmin, and 0 at xmax; of course, in principle we have the
   // unbinned information available in the trees, but the unbinned minimization is
   // too slow, and we don't need to do a precision measurement here. Hence, we force
   // this property.
   Double_t eps = 1.0e-5;
   if      (theCut-fXmin < eps) retval = (GetCutOrientation() == kPositive) ? 1.0 : 0.0;
   else if (fXmax-theCut < eps) retval = (GetCutOrientation() == kPositive) ? 0.0 : 1.0;

   return retval;
}

//_______________________________________________________________________
const std::vector<TMVA::Event*>& TMVA::MethodBase::GetEventCollection( Types::ETreeType type) 
{
   if (GetTransformationHandler().GetTransformationList().GetEntries() <= 0) {
      return (Data()->GetEventCollection(type));
   }
   Int_t idx = Data()->TreeIndex(type);
   if (fEventCollections.at(idx) == 0) {
      fEventCollections.at(idx) = &(Data()->GetEventCollection(type));
      fEventCollections.at(idx) = GetTransformationHandler().CalcTransformations(*(fEventCollections.at(idx)),kTRUE);
   }
   return *(fEventCollections.at(idx));
}

//_______________________________________________________________________
TString TMVA::MethodBase::GetTrainingTMVAVersionString() const
{
   // calculates the TMVA version string from the training version code on the fly
   UInt_t a = GetTrainingTMVAVersionCode() & 0xff0000; a>>=16;
   UInt_t b = GetTrainingTMVAVersionCode() & 0x00ff00; b>>=8;
   UInt_t c = GetTrainingTMVAVersionCode() & 0x0000ff;

   return TString(Form("%i.%i.%i",a,b,c));
}

//_______________________________________________________________________
TString TMVA::MethodBase::GetTrainingROOTVersionString() const
{
   // calculates the ROOT version string from the training version code on the fly
   UInt_t a = GetTrainingROOTVersionCode() & 0xff0000; a>>=16;
   UInt_t b = GetTrainingROOTVersionCode() & 0x00ff00; b>>=8;
   UInt_t c = GetTrainingROOTVersionCode() & 0x0000ff;

   return TString(Form("%i.%02i/%02i",a,b,c));
}
 
//_______________________________________________________________________
TMVA::MethodBase* TMVA::MethodBase::GetThisBase()
{
   // return a pointer the base class of this method
   return fgThisBase; 
}

//_______________________________________________________________________
void TMVA::MethodBase::ResetThisBase() 
{ 
   // reset required for RootFinder
   fgThisBase = this; 
}
