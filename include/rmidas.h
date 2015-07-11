/********************************************************************\

  Name:         RMIDAS.H
  Created by:   Stefan Ritt & Konstantin Olchanski

  Contents:     ROOT definitions for MIDAS applications

\********************************************************************/

#ifndef _RMIDAS_H_
#define _RMIDAS_H_

#include <TObjArray.h>
#include <TFolder.h>
#include <TCutG.h>

/* root functions really are C++ functions */ extern TFolder *gManaHistosFolder;
extern TObjArray *gHistoFolderStack;

// book functions put a root object in a suitable folder
// for histos, there are a lot of types, so we use templates.
// for other objects we have one function per object
template < typename TH1X >
TH1X EXPRT * h1_book(const char *name, const char *title,
                     int bins, double min, double max)
{
   TH1X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH1X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH1X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH1X(name, title, bins, min, max);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

template < typename TH1X >
TH1X EXPRT * h1_book(const char *name, const char *title, int bins, double edges[])
{
   TH1X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH1X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH1X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH1X(name, title, bins, edges);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

template < typename TH2X >
TH2X EXPRT * h2_book(const char *name, const char *title,
                     int xbins, double xmin, double xmax,
                     int ybins, double ymin, double ymax)
{
   TH2X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH2X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH2X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH2X(name, title, xbins, xmin, xmax, ybins, ymin, ymax);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

template < typename TH2X >
TH2X EXPRT * h2_book(const char *name, const char *title,
                     int xbins, double xmin, double xmax, int ybins, double yedges[])
{
   TH2X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH2X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH2X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH2X(name, title, xbins, xmin, xmax, ybins, yedges);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

template < typename TH2X >
TH2X EXPRT * h2_book(const char *name, const char *title,
                     int xbins, double xedges[], int ybins, double ymin, double ymax)
{
   TH2X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH2X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH2X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH2X(name, title, xbins, xedges, ybins, ymin, ymax);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

template < typename TH2X >
TH2X EXPRT * h2_book(const char *name, const char *title,
                     int xbins, double xedges[], int ybins, double yedges[])
{
   TH2X *hist;

   /* check if histo already exists */
   if (!gHistoFolderStack->Last())
      hist = (TH2X *) gManaHistosFolder->FindObjectAny(name);
   else
      hist = (TH2X *) ((TFolder *) gHistoFolderStack->Last())->FindObjectAny(name);

   if (hist == NULL) {
      hist = new TH2X(name, title, xbins, xedges, ybins, yedges);
      if (!gHistoFolderStack->Last())
         gManaHistosFolder->Add(hist);
      else
         ((TFolder *) gHistoFolderStack->Last())->Add(hist);
   }

   return hist;
}

/*
 * the following two macros allow for simple fortran like usage
 * for the most common histo types
 */
#define H1_BOOK(n,t,b,min,max) (h1_book<TH1F>(n,t,b,min,max))
#define H2_BOOK(n,t,xb,xmin,xmax,yb,ymin,ymax) (h2_book<TH2F>(n,t,xb,xmin,xmax,yb,ymin,ymax))

TCutG *cut_book(const char *name);

#endif
