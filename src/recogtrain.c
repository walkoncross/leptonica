/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -
 -  Redistribution and use in source and binary forms, with or without
 -  modification, are permitted provided that the following conditions
 -  are met:
 -  1. Redistributions of source code must retain the above copyright
 -     notice, this list of conditions and the following disclaimer.
 -  2. Redistributions in binary form must reproduce the above
 -     copyright notice, this list of conditions and the following
 -     disclaimer in the documentation and/or other materials
 -     provided with the distribution.
 -
 -  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 -  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 -  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 -  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANY
 -  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 -  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 -  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 -  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 -  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 -  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 -  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *====================================================================*/

/*!
 * \file recogtrain.c
 * <pre>
 *
 *      Training on labeled data
 *         l_int32             recogTrainLabeled()
 *         PIX                *recogProcessSingleLabeled()
 *         l_int32             recogProcessMultLabeled()
 *         l_int32             recogAddSamples()
 *         PIX                *recogModifyTemplate()
 *         l_int32             recogAverageSamples()
 *         l_int32             pixaAccumulateSamples()
 *         l_int32             recogTrainingFinished()
 *         PIXA               *recogRemoveOutliers()
 *
 *      Training on unlabeled data
 *         L_RECOG             recogTrainFromBoot()
 *
 *      Padding the digit training set
 *         l_int32             recogPadDigitTrainingSet()
 *         l_int32             recogIsPaddingNeeded()
 *         static SARRAY      *recogAddMissingClassStrings()
 *         PIXA               *recogAddDigitPadTemplates()
 *         static l_int32      recogCharsetAvailable()
 *
 *      Making a boot digit recognizer
 *         L_RECOG            *recogMakeBootDigitRecog()
 *         PIXA               *recogMakeBootDigitTemplates()
 *
 *      Debugging
 *         l_int32             recogShowContent()
 *         l_int32             recogDebugAverages()
 *         l_int32             recogShowAverageTemplates()
 *         PIX                *recogDisplayOutliers()
 *         PIX                *recogShowMatchesInRange()
 *         PIX                *recogShowMatch()
 *
 *      Static helper
 *         static char        *l_charToString()
 *
 *  These abbreviations are for the type of template to be used:
 *    * SI (for the scanned images)
 *    * WNL (for width-normalized lines, formed by first skeletonizing
 *           the scanned images, and then dilating to a fixed width)
 *  These abbreviations are for the type of recognizer:
 *    * BAR (book-adapted recognizer; the best type; can do identification
 *           with unscaled images and separation of touching characters.
 *    * BSR (bootstrap recognizer; used if more labeled templates are
 *           required for a BAR, either for finding more templates from
 *           the book, or making a hybrid BAR/BSR.
 *
 *  The recog struct typically holds two versions of the input templates
 *  (e.g. from a pixa) that were used to generate it.  One version is
 *  the unscaled input templates.  The other version is the one that
 *  will be used by the recog to identify unlabeled data.  That version
 *  depends on the input parameters when the recog is created.  The choices
 *  for the latter version, and their suggested use, are:
 *  (1) unscaled SI -- typical for BAR, generated from book images
 *  (2) unscaled WNL -- ditto
 *  (3) scaled SI -- typical for recognizers containing template
 *      images from sources other than the book to be recognized
 *  (4) scaled WNL -- ditto
 *  For cases (3) and (4), we recommend scaling to fixed height; e.g.,
 *  scalew = 0, scaleh = 40.
 *  When using WNL, we recommend using a width of 5 in the template
 *  and 4 in the unlabeled data.
 *  It appears that better results for a BAR are usually obtained using
 *  SI than WNL, but more experimentation is needed.
 *  
 *  This utility is designed to build recognizers that are specifically
 *  adapted from a large amount of material, such as a book.  These
 *  use labeled templates taken from the material, and not scaled.
 *  In addition, two special recognizers are useful:
 *  (1) Bootstrap recognizer (BSR).  This uses height-scaled templates,
 *      that have been extended with several repetitions in one of two ways:
 *      (a) aniotropic width scaling (for either SI or WNL)
 *      (b) iterative erosions/dilations (for SI).
 *  (2) Outlier removal.  This uses height scaled templates.  It can be
 *      implemented without using templates that are aligned averages of all
 *      templates in a class.
 *
 *  Recognizers are inexpensive to generate, for example, from a pixa
 *  of labeled templates.  The general process of building a BAR is
 *  to start with labeled templates, e.g., in a pixa, make a BAR, and
 *  analyze new samples from the book to augment the BAR until it has
 *  enough samples for each character class.  Along the way, samples
 *  from a BSR may be added for help in training.  If not enough samples
 *  are available for the BAR, it can finally be augmented with BSR
 *  samples, in which case the resulting hybrid BAR/BSR recognizer
 *  must work on scaled images.
 *
 *  Here are the steps in doing recog training:
 *  A. Generate a BAR from any exising labeled templates
 *    (1) Create a recog and add the templates, using recogAddSamples().
 *        This stores the unscaled templates.
 *        [Note: this can be done in one step if the labeled templates are put
 *         into a pixa:
 *           L_Recog *rec = recogCreateFromPixa(pixa, ...);  ]
 *    (2) Call recogTrainingFinished() to generate the (sometimes modified)
 *        templates to be used for correlation.
 *    (3) Optionally, remove outliers.
 *    If there are sufficient samples in the classes, we're done. Otherwise,
 *  B. Try to get more samples from the book to pad the BAR.
 *     (1) Save the unscaled, labeled templates from the BAR.
 *     (2) Supplement the BAR with bootstrap templates to make a hybrid BAR/BSR.
 *     (3) Do recognition on more unlabeled images, scaled to a fixed height
 *     (4) Add the unscaled, labeled images to the saved set.
 *     (5) Optionally, remove outliers.
 *     If there are sufficient samples in the classes, we're done. Otherwise,
 *  C. For classes without a sufficient number of templates, we must again
 *     supplement the BAR with templates from a BSR (a hybrid RAR/BSR);
 *     do recognition scaled to a fixed height.
 *
 *  Here are several methods that can be used for identifying outliers:
 *  (1) Compute average templates for each class and remove a candidate
 *      that is poorly correlated with the average.  This is the most
 *      simple method.
 *  (2) Compute average templates for each class and remove a candidate
 *      that is more highly correlated with the average of some other class.
 *      This does not require setting a threshold for the correlation.
 *  (3) For each candidate, find the average correlation with other
 *      members of its class, and remove those that have a relatively
 *      low average correlation.  This is similar to (1), gives comparable
 *      results and requires a bit more computation, but it does not
 *      require computing the average templates.
 *  We are presently using method (1).
 * </pre>
 */

#include <string.h>
#include "allheaders.h"

    /* Static functions */
static l_int32 *recogMapIndexToIndex(L_RECOG *recog1, L_RECOG *recog2);
static l_int32 recogAverageClassGeom(L_RECOG *recog, NUMA **pnaw, NUMA **pnah);
static SARRAY *recogAddMissingClassStrings(L_RECOG  *recog);
static l_int32 recogCharsetAvailable(l_int32 type);
static char *l_charToString(char byte);

    /* Defaults in pixRemoveOutliers() */
static const l_float32  DEFAULT_MIN_SCORE = 0.75; /* keep everything above */
static const l_float32  DEFAULT_MIN_FRACTION = 0.5;  /* to be kept */

/*------------------------------------------------------------------------*
 *                                Training                                *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogTrainLabeled()
 *
 * \param[in]    recog in training mode
 * \param[in]    pixs if depth > 1, will be thresholded to 1 bpp
 * \param[in]    box [optional] cropping box
 * \param[in]    text [optional] if null, use text field in pix
 * \param[in]    multflag 1 if one or more contiguous ascii characters;
 *                        0 for a single arbitrary character
 * \param[in]    debug 1 to display images of samples not captured
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) Training is restricted to the addition of either:
 *          (a) multflag == 0: a single character in an arbitrary
 *              (e.g., UTF8) charset
 *          (b) multflag == 1: one or more ascii characters rendered
 *              contiguously in pixs
 *      (2) If box != null, it should represent the cropped location of
 *          the character image.
 *      (3) If multflag == 1, samples will be rejected if the number of
 *          connected components does not equal to the number of ascii
 *          characters in the textstring.  In that case, if debug == 1,
 *          the rejected samples will be displayed.
 * </pre>
 */
l_int32
recogTrainLabeled(L_RECOG  *recog,
                  PIX      *pixs,
                  BOX      *box,
                  char     *text,
                  l_int32   multflag,
                  l_int32   debug)
{
l_int32  ret;
PIXA    *pixa;

    PROCNAME("recogTrainLabeled");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

    if (multflag == 0) {
        ret = recogProcessSingleLabeled(recog, pixs, box, text, &pixa);
    } else {
        ret = recogProcessMultLabeled(recog, pixs, box, text, &pixa, debug);
    }
    if (ret)
        return ERROR_INT("failure to add training data", procName, 1);
    recogAddSamples(recog, pixa, -1, debug);
    pixaDestroy(&pixa);
    return 0;
}


/*!
 * \brief   recogProcessSingleLabeled()
 *
 * \param[in]    recog in training mode
 * \param[in]    pixs if depth > 1, will be thresholded to 1 bpp
 * \param[in]    box [optional] cropping box
 * \param[in]    text [optional] if null, use text field in pix
 * \param[out]   ppixa one pix, 1 bpp, labeled
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This crops and binarizes the input image, generating a pix
 *          of one character where the charval is inserted into the pix.
 * </pre>
 */
l_int32
recogProcessSingleLabeled(L_RECOG  *recog,
                          PIX      *pixs,
                          BOX      *box,
                          char     *text,
                          PIXA    **ppixa)
{
char    *textdata;
l_int32  textinpix, textin;
PIX     *pixc, *pixb, *pixd;

    PROCNAME("recogProcessSingleLabeled");

    if (!ppixa)
        return ERROR_INT("&pixa not defined", procName, 1);
    *ppixa = NULL;
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

        /* Find the text; this will be stored with the output images */
    textin = text && (text[0] != '\0');
    textinpix = (pixs->text && (pixs->text[0] != '\0'));
    if (!textin && !textinpix) {
        L_ERROR("no text: %d\n", procName, recog->num_samples);
        return 1;
    }
    textdata = (textin) ? text : pixs->text;  /* do not free */

        /* Crop and binarize if necessary */
    if (box)
        pixc = pixClipRectangle(pixs, box, NULL);
    else
        pixc = pixClone(pixs);
    if (pixGetDepth(pixc) > 1)
        pixb = pixConvertTo1(pixc, recog->threshold);
    else
        pixb = pixClone(pixc);
    pixDestroy(&pixc);

        /* Clip to foreground and save */
    pixClipToForeground(pixb, &pixd, NULL);
    pixDestroy(&pixb);
    if (!pixd)
        return ERROR_INT("pixd is empty", procName, 1);
    pixSetText(pixd, textdata);
    *ppixa = pixaCreate(1);
    pixaAddPix(*ppixa, pixd, L_INSERT);
    return 0;
}


/*!
 * \brief   recogProcessMultLabeled()
 *
 * \param[in]    recog in training mode
 * \param[in]    pixs if depth > 1, will be thresholded to 1 bpp
 * \param[in]    box [optional] cropping box
 * \param[in]    text [optional] if null, use text field in pix
 * \param[out]   ppixa of split and thresholded characters
 * \param[in]    debug 1 to display images of samples not captured
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This crops and segments one or more labeled and contiguous
 *          ascii characters, for input in training.  It is a special case.
 *      (2) The character images are bundled into a pixa with the
 *          character text data embedded in each pix.
 *      (3) Where there is more than one character, this does some
 *          noise reduction and extracts the resulting character images
 *          from left to right.  No scaling is performed.
 * </pre>
 */
l_int32
recogProcessMultLabeled(L_RECOG  *recog,
                        PIX      *pixs,
                        BOX      *box,
                        char     *text,
                        PIXA    **ppixa,
                        l_int32   debug)
{
char      *textdata, *textstr;
l_int32    textinpix, textin, nchars, ncomp, i;
BOX       *box2;
BOXA      *boxa1, *boxa2, *boxa3, *boxa4;
PIX       *pixc, *pixb, *pix1, *pix2;

    PROCNAME("recogProcessMultLabeled");

    if (!ppixa)
        return ERROR_INT("&pixa not defined", procName, 1);
    *ppixa = NULL;
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!pixs)
        return ERROR_INT("pixs not defined", procName, 1);

        /* Find the text; this will be stored with the output images */
    textin = text && (text[0] != '\0');
    textinpix = pixs->text && (pixs->text[0] != '\0');
    if (!textin && !textinpix) {
        L_ERROR("no text: %d\n", procName, recog->num_samples);
        return 1;
    }
    textdata = (textin) ? text : pixs->text;  /* do not free */

        /* Crop and binarize if necessary */
    if (box)
        pixc = pixClipRectangle(pixs, box, NULL);
    else
        pixc = pixClone(pixs);
    if (pixGetDepth(pixc) > 1)
        pixb = pixConvertTo1(pixc, recog->threshold);
    else
        pixb = pixClone(pixc);
    pixDestroy(&pixc);

        /* We segment the set of characters as follows:
         * (1) A large vertical closing should consolidate most characters.
               Do not attempt to split touching characters using openings,
               because this is likely to break actual characters. */
    pix1 = pixMorphSequence(pixb, "c1.70", 0);

        /* (2) Include overlapping components and remove small ones */
    boxa1 = pixConnComp(pix1, NULL, 8);
    boxa2 = boxaCombineOverlaps(boxa1);
    boxa3 = boxaSelectBySize(boxa2, 2, 8, L_SELECT_IF_BOTH,
                             L_SELECT_IF_GT, NULL);
    pixDestroy(&pix1);
    boxaDestroy(&boxa1);
    boxaDestroy(&boxa2);

        /* (3) Make sure the components equal the number of text characters */
    ncomp = boxaGetCount(boxa3);
    nchars = strlen(textdata);
    if (ncomp != nchars) {
        L_ERROR("ncomp (%d) != nchars (%d); num samples = %d\n",
                procName, ncomp, nchars, recog->num_samples);
        if (debug) {
            pix1 = pixConvertTo32(pixb);
            pixRenderBoxaArb(pix1, boxa3, 1, 255, 0, 0);
            pixDisplay(pix1, 10 * recog->num_samples, 100);
            pixDestroy(&pix1);
        }
        pixDestroy(&pixb);
        boxaDestroy(&boxa3);
        return 1;
    }

        /* (4) Sort the components from left to right and extract them */
    boxa4 = boxaSort(boxa3, L_SORT_BY_X, L_SORT_INCREASING, NULL);
    boxaDestroy(&boxa3);

        /* Save the results, with one character in each pix */
    *ppixa = pixaCreate(ncomp);
    for (i = 0; i < ncomp; i++) {
        box2 = boxaGetBox(boxa4, i, L_CLONE);
        pix2 = pixClipRectangle(pixb, box2, NULL);
        textstr = l_charToString(textdata[i]);
        pixSetText(pix2, textstr);  /* inserts a copy */
        pixaAddPix(*ppixa, pix2, L_INSERT);
        boxDestroy(&box2);
        LEPT_FREE(textstr);
    }

    pixDestroy(&pixb);
    boxaDestroy(&boxa4);
    return 0;
}


/*!
 * \brief   recogAddSamples()
 *
 * \param[in]    recog
 * \param[in]    pixa 1 or more characters
 * \param[in]    classindex use -1 if not forcing into a specified class
 * \param[in]    debug
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) The pix in the pixa are all 1 bpp, and the character string
 *          labels are embedded in the pix.
 *      (2) Note: this function decides what class each pix belongs in.
 *          When input is from a multifont pixaa, with a valid value
 *          for %classindex, the character string label in each pix
 *          is ignored, and %classindex is used as the class index
 *          for all the pix in the pixa.  Thus, for that situation we
 *          use this class index to avoid making the decision through a
 *          lookup based on the character strings embedded in the pix.
 *      (3) When a recog is initially filled with samples, the pixaa_u
 *          array is initialized to accept up to 256 different classes.
 *          When training is finished, the arrays are truncated to the
 *          actual number of classes.  To pad an existing recog from
 *          the boot recognizers, training is started again; if samples
 *          from a new class are added, the pixaa_u array must be
 *          extended by adding a pixa to hold them.
 * </pre>
 */
l_int32
recogAddSamples(L_RECOG  *recog,
                PIXA     *pixa,
                l_int32   classindex,
                l_int32   debug)
{
char    *text;
l_int32  i, n, npa, charint, index;
PIX     *pixb;
PIXA    *pixa1;
PIXAA   *paa;

    PROCNAME("recogAddSamples");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!pixa) {
        L_ERROR("pixa not defined: %d\n", procName, recog->num_samples);
        return 1;
    }
    if (recog->train_done)
        return ERROR_INT("training has been completed", procName, 1);
    if ((n = pixaGetCount(pixa)) == 0)
        ERROR_INT("no pix in the pixa", procName, 1);
    paa = recog->pixaa_u;

    for (i = 0; i < n; i++) {
        pixb = pixaGetPix(pixa, i, L_CLONE);
        if (classindex < 0) {
                /* Determine the class array index.  Check if the class
                 * alreadly exists, and if not, add it. */
            text = pixGetText(pixb);
            if (l_convertCharstrToInt(text, &charint) == 1) {
                L_ERROR("invalid text: %s\n", procName, text);
                pixDestroy(&pixb);
                continue;
            }
            if (recogGetClassIndex(recog, charint, text, &index) == 1) {
                    /* New class must be added */
                npa = pixaaGetCount(paa, NULL);
                if (index > npa)
                    L_ERROR("index %d > npa %d!!\n", procName, index, npa);
                if (index == npa) {  /* paa needs to be extended */
                    L_INFO("Adding new class and pixa: index = %d, text = %s\n",
                           procName, index, text);
                    pixa1 = pixaCreate(10);
                    pixaaAddPixa(paa, pixa1, L_INSERT);
                }
            }
            if (debug) {
                L_INFO("Identified text label: %s\n", procName, text);
                L_INFO("Identified: charint = %d, index = %d\n",
                       procName, charint, index);
            }
        } else {
            index = classindex;
        }

            /* Insert the unscaled character image into the right pixa.
             * (Unscaled images are required to split touching characters.) */
        recog->num_samples++;
        pixaaAddPix(paa, index, pixb, NULL, L_INSERT);
    }

    return 0;
}


/*!
 * \brief   recogModifyTemplate()
 *
 * \param[in]    recog
 * \param[in]    pixs   1 bpp, to be optionally scaled and turned into
 *                      strokes of fixed width
 * \return  pixd   modified pix if OK, NULL on error
 */
PIX *
recogModifyTemplate(L_RECOG  *recog,
                    PIX      *pixs)
{
l_int32  w, h;
PIX     *pix1, *pix2;

    PROCNAME("recogModifyTemplate");

    if (!recog)
        return (PIX *)ERROR_PTR("recog not defined", procName, NULL);
    if (!pixs)
        return (PIX *)ERROR_PTR("pixs not defined", procName, NULL);

        /* Scale first */
    pixGetDimensions(pixs, &w, &h, NULL);
    if ((recog->scalew == 0 || recog->scalew == w) &&
        (recog->scaleh == 0 || recog->scaleh == h)) {  /* no scaling */
        pix1 = pixCopy(NULL, pixs);
    } else {
        pix1 = pixScaleToSize(pixs, recog->scalew, recog->scaleh);
    }

        /* Then optionally convert to lines */
    if (recog->linew <= 0) {
        pix2 = pixClone(pix1);
    } else {
        pix2 = pixSetStrokeWidth(pix1, recog->linew, 1, 8);
    }
    pixDestroy(&pix1);

    return pix2;
}


/*!
 * \brief   recogAverageSamples()
 *
 * \param[in]    recog
 * \param[in]    debug
 * \return  0 on success, 1 on failure
 *
 * <pre>
 * Notes:
 *      (1) This is only called:
 *          (a) when splitting characters using the greedy splitter
 *              recogCorrelationBestRow(), and
 *          (b) by a special recognizer that is used to remove outliers.
 *          Both unscaled and scaled inputs are averaged.
 *      (2) Set debug = 1 to view the resulting templates and their centroids.
 * </pre>
 */
l_int32
recogAverageSamples(L_RECOG  *recog,
                    l_int32   debug)
{
l_int32    i, nsamp, size, area;
l_float32  x, y;
PIXA      *pixat, *pixa_sel;
PIX       *pix1, *pix2;
PTA       *ptat;

    PROCNAME("recogAverageSamples");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    if (recog->ave_done) {
        if (debug)  /* always do this if requested */
            recogShowAverageTemplates(recog);
        return 0;
    }

        /* Remove any previous averaging data */
    size = recog->setsize;
    pixaDestroy(&recog->pixa_u);
    ptaDestroy(&recog->pta_u);
    numaDestroy(&recog->nasum_u);
    recog->pixa_u = pixaCreate(size);
    recog->pta_u = ptaCreate(size);
    recog->nasum_u = numaCreate(size);

    pixaDestroy(&recog->pixa);
    ptaDestroy(&recog->pta);
    numaDestroy(&recog->nasum);
    recog->pixa = pixaCreate(size);
    recog->pta = ptaCreate(size);
    recog->nasum = numaCreate(size);

        /* Unscaled bitmaps: compute averaged bitmap, centroid, and fg area */
    for (i = 0; i < size; i++) {
        pixat = pixaaGetPixa(recog->pixaa_u, i, L_CLONE);
        ptat = ptaaGetPta(recog->ptaa_u, i, L_CLONE);
        nsamp = pixaGetCount(pixat);
        nsamp = L_MIN(nsamp, 256);  /* we only use the first 256 */
        if (nsamp == 0) {  /* no information for this class */
            pix1 = pixCreate(1, 1, 1);
            pixaAddPix(recog->pixa_u, pix1, L_INSERT);
            ptaAddPt(recog->pta_u, 0, 0);
            numaAddNumber(recog->nasum_u, 0);
        } else {
            pixaAccumulateSamples(pixat, ptat, &pix1, &x, &y);
            nsamp = (nsamp == 1) ? 2 : nsamp;  /* special case thresh */
            pix2 = pixThresholdToBinary(pix1, nsamp / 2);
            pixInvert(pix2, pix2);
            pixaAddPix(recog->pixa_u, pix2, L_INSERT);
            ptaAddPt(recog->pta_u, x, y);
            pixCountPixels(pix2, &area, recog->sumtab);
            numaAddNumber(recog->nasum_u, area);  /* foreground */
            pixDestroy(&pix1);
        }
        pixaDestroy(&pixat);
        ptaDestroy(&ptat);
    }

        /* Any classes for which there are no samples will have a 1x1
         * pix as a placeholder.  This must not be included when
         * finding the size range of the averaged templates. */
    pixa_sel = pixaSelectBySize(recog->pixa_u, 5, 5, L_SELECT_IF_BOTH,
                                L_SELECT_IF_GTE, NULL);
    pixaSizeRange(pixa_sel, &recog->minwidth_u, &recog->minheight_u,
                  &recog->maxwidth_u, &recog->maxheight_u);
    pixaDestroy(&pixa_sel);

        /* Scaled bitmaps: compute averaged bitmap, centroid, and fg area */
    for (i = 0; i < size; i++) {
        pixat = pixaaGetPixa(recog->pixaa, i, L_CLONE);
        ptat = ptaaGetPta(recog->ptaa, i, L_CLONE);
        nsamp = pixaGetCount(pixat);
        nsamp = L_MIN(nsamp, 256);  /* we only use the first 256 */
        if (nsamp == 0) {  /* no information for this class */
            pix1 = pixCreate(1, 1, 1);
            pixaAddPix(recog->pixa, pix1, L_INSERT);
            ptaAddPt(recog->pta, 0, 0);
            numaAddNumber(recog->nasum, 0);
        } else {
            pixaAccumulateSamples(pixat, ptat, &pix1, &x, &y);
            nsamp = (nsamp == 1) ? 2 : nsamp;  /* special case thresh */
            pix2 = pixThresholdToBinary(pix1, nsamp / 2);
            pixInvert(pix2, pix2);
            pixaAddPix(recog->pixa, pix2, L_INSERT);
            ptaAddPt(recog->pta, x, y);
            pixCountPixels(pix2, &area, recog->sumtab);
            numaAddNumber(recog->nasum, area);  /* foreground */
            pixDestroy(&pix1);
        }
        pixaDestroy(&pixat);
        ptaDestroy(&ptat);
    }
    pixa_sel = pixaSelectBySize(recog->pixa, 5, 5, L_SELECT_IF_BOTH,
                                L_SELECT_IF_GTE, NULL);
    pixaSizeRange(pixa_sel, &recog->minwidth, NULL, &recog->maxwidth, NULL);
    pixaDestroy(&pixa_sel);

       /* Get min and max splitting dimensions */
    recog->min_splitw = L_MAX(5, recog->minwidth_u - 5);
    recog->min_splith = L_MAX(5, recog->minheight_u - 5);
    recog->max_splith = recog->maxheight_u + 12;  /* allow for skew */

    if (debug)
        recogShowAverageTemplates(recog);

    recog->ave_done = TRUE;
    return 0;
}


/*!
 * \brief   pixaAccumulateSamples()
 *
 * \param[in]    pixa of samples from the same class, 1 bpp
 * \param[in]    pta [optional] of centroids of the samples
 * \param[out]   ppixd accumulated samples, 8 bpp
 * \param[out]   px [optional] average x coordinate of centroids
 * \param[out]   py [optional] average y coordinate of centroids
 * \return  0 on success, 1 on failure
 *
 * <pre>
 * Notes:
 *      (1) This generates an aligned (by centroid) sum of the input pix.
 *      (2) We use only the first 256 samples; that's plenty.
 *      (3) If pta is not input, we generate two tables, and discard
 *          after use.  If this is called many times, it is better
 *          to precompute the pta.
 * </pre>
 */
l_int32
pixaAccumulateSamples(PIXA       *pixa,
                      PTA        *pta,
                      PIX       **ppixd,
                      l_float32  *px,
                      l_float32  *py)
{
l_int32    i, n, maxw, maxh, xdiff, ydiff;
l_int32   *centtab, *sumtab;
l_float32  x, y, xave, yave;
PIX       *pix1, *pix2, *pixsum;
PTA       *ptac;

    PROCNAME("pixaAccumulateSamples");

    if (px) *px = 0;
    if (py) *py = 0;
    if (!ppixd)
        return ERROR_INT("&pixd not defined", procName, 1);
    *ppixd = NULL;
    if (!pixa)
        return ERROR_INT("pixa not defined", procName, 1);

    n = pixaGetCount(pixa);
    if (pta && ptaGetCount(pta) != n)
        return ERROR_INT("pta count differs from pixa count", procName, 1);
    n = L_MIN(n, 256);  /* take the first 256 only */
    if (n == 0)
        return ERROR_INT("pixa array empty", procName, 1);

    if (pta) {
        ptac = ptaClone(pta);
    } else {  /* generate them here */
        ptac = ptaCreate(n);
        centtab = makePixelCentroidTab8();
        sumtab = makePixelSumTab8();
        for (i = 0; i < n; i++) {
            pix1 = pixaGetPix(pixa, i, L_CLONE);
            pixCentroid(pix1, centtab, sumtab, &xave, &yave);
            ptaAddPt(ptac, xave, yave);
        }
        LEPT_FREE(centtab);
        LEPT_FREE(sumtab);
    }

        /* Find the average value of the centroids */
    xave = yave = 0;
    for (i = 0; i < n; i++) {
        ptaGetPt(pta, i, &x, &y);
        xave += x;
        yave += y;
    }
    xave = xave / (l_float32)n;
    yave = yave / (l_float32)n;
    if (px) *px = xave;
    if (py) *py = yave;

        /* Place all centroids at their average value and sum the results */
    pixaSizeRange(pixa, NULL, NULL, &maxw, &maxh);
    pixsum = pixInitAccumulate(maxw, maxh, 0);
    pix1 = pixCreate(maxw, maxh, 1);

    for (i = 0; i < n; i++) {
        pix2 = pixaGetPix(pixa, i, L_CLONE);
        ptaGetPt(ptac, i, &x, &y);
        xdiff = (l_int32)(x - xave);
        ydiff = (l_int32)(y - yave);
        pixClearAll(pix1);
        pixRasterop(pix1, xdiff, ydiff, maxw, maxh, PIX_SRC,
                    pix2, 0, 0);
        pixAccumulate(pixsum, pix1, L_ARITH_ADD);
        pixDestroy(&pix2);
    }
    *ppixd = pixFinalAccumulate(pixsum, 0, 8);

    pixDestroy(&pix1);
    pixDestroy(&pixsum);
    ptaDestroy(&ptac);
    return 0;
}


/*!
 * \brief   recogTrainingFinished()
 *
 * \param[in]    recog
 * \param[in]    modifyflag
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This must be called after all training samples have been added.
 *      (2) Usually, %modifyflag == 1, because we want to apply
 *          recogModifyTemplate() to generate the actual templates 
 *          that will be used.  The one exception is when reading a
 *          serialized recog: there we want to put the same set of
 *          templates in both the unscaled and modified pixaa.
 *          See recogReadStream() to see why we do this.
 *      (3) The following things are done here:
 *          (a) Allocate (or reallocate) storage for (possibly) modified
 *              bitmaps, centroids, and fg areas.
 *          (b) Generate the (possibly) modified bitmaps.
 *          (c) Compute centroid and fg area data for both unscaled and
 *              modified bitmaps.
 *          (d) Truncate the pixaa, ptaa and numaa arrays down from
 *              256 to the actual size.
 *      (4) Putting these operations here makes it simple to recompute
 *          the recog with different modifications on the bitmaps.
 *      (5) Call recogShowContent() to display the templates, both
 *          unscaled and modified.
 * </pre>
 */
l_int32
recogTrainingFinished(L_RECOG  *recog,
                      l_int32   modifyflag)
{
l_int32    i, j, size, nc, ns, area;
l_float32  xave, yave;
PIX       *pix, *pixd;
PIXA      *pixa;
PIXAA     *paa;
PTA       *pta;
PTAA      *ptaa;

    PROCNAME("recogTrainingFinished");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (recog->train_done) return 0;

        /* Generate the storage for the possibly-scaled training bitmaps */
    size = recog->maxarraysize;
    paa = pixaaCreate(size);
    pixa = pixaCreate(1);
    pixaaInitFull(paa, pixa);
    pixaDestroy(&pixa);
    pixaaDestroy(&recog->pixaa);
    recog->pixaa = paa;

        /* Generate the storage for the unscaled centroid training data */
    ptaa = ptaaCreate(size);
    pta = ptaCreate(0);
    ptaaInitFull(ptaa, pta);
    ptaaDestroy(&recog->ptaa_u);
    recog->ptaa_u = ptaa;

        /* Generate the storage for the possibly-scaled centroid data */
    ptaa = ptaaCreate(size);
    ptaaInitFull(ptaa, pta);
    ptaDestroy(&pta);
    ptaaDestroy(&recog->ptaa);
    recog->ptaa = ptaa;

        /* Generate the storage for the fg area data */
    numaaDestroy(&recog->naasum_u);
    numaaDestroy(&recog->naasum);
    recog->naasum_u = numaaCreateFull(size, 0);
    recog->naasum = numaaCreateFull(size, 0);

    paa = recog->pixaa_u;
    nc = recog->setsize;
    for (i = 0; i < nc; i++) {
        pixa = pixaaGetPixa(paa, i, L_CLONE);
        ns = pixaGetCount(pixa);
        for (j = 0; j < ns; j++) {
                /* Save centroid and area data for the unscaled pix */
            pix = pixaGetPix(pixa, j, L_CLONE);
            pixCentroid(pix, recog->centtab, recog->sumtab, &xave, &yave);
            ptaaAddPt(recog->ptaa_u, i, xave, yave);
            pixCountPixels(pix, &area, recog->sumtab);
            numaaAddNumber(recog->naasum_u, i, area);  /* foreground */

                /* Insert the (optionally) scaled character image, and
                 * save centroid and area data for it */
            if (modifyflag == 1)
                pixd = recogModifyTemplate(recog, pix);
            else
                pixd = pixClone(pix);
            pixaaAddPix(recog->pixaa, i, pixd, NULL, L_INSERT);
            pixCentroid(pixd, recog->centtab, recog->sumtab, &xave, &yave);
            ptaaAddPt(recog->ptaa, i, xave, yave);
            pixCountPixels(pixd, &area, recog->sumtab);
            numaaAddNumber(recog->naasum, i, area);
            pixDestroy(&pix);
        }
        pixaDestroy(&pixa);
    }

        /* Truncate the arrays to those with non-empty containers */
    pixaaTruncate(recog->pixaa_u);
    pixaaTruncate(recog->pixaa);
    ptaaTruncate(recog->ptaa_u);
    ptaaTruncate(recog->ptaa);
    numaaTruncate(recog->naasum_u);
    numaaTruncate(recog->naasum);

    recog->train_done = TRUE;
    return 0;
}


/*!
 * \brief   recogRemoveOutliers()
 *
 * \param[in]   pixas        unscaled labeled templates
 * \param[in]   minscore     keep everything with at least this score
 * \param[in]   minfract     minimum fraction to retain
 * \param[out]  pixarem      [optional debug] removed templates
 * \param[out]  narem        [optional debug] scores of removed templates
 * \return  pixa   of unscaled templates to be kept, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) Removing outliers is particularly important when recognition
 *          goes against all the samples in the training set, as opposed
 *          to the averages for each class.  The reason is that we get
 *          an identification error if a mislabeled template is a best
 *          match for an input sample.
 *      (2) Because the score values depend strongly on the quality
 *          of the character images, to avoid losing too many samples
 *          we supplement a minimum score for retention with a minimum
 *          fraction that we must keep.  Consequently, with poor quality
 *          images, we may keep samples with a score less than the
 *          %minscore, in order to satisfy the %minfract requirement.
 *          In addition, require that at least one sample will be retained.
 *      (3) This is meant to be used on a BAR, Where the templates all
 *          come from the same book; use minscore ~0.75.
 *      (4) Method: make a scaled recog from the input %pixas.  Then,
 *          for each class: generate the averages, match each
 *          scaled template against the average, and save unscaled
 *          templates that had a sufficiently good match.
 * </pre>
 */
PIXA *
recogRemoveOutliers(PIXA      *pixas,
                    l_float32  minscore,
                    l_float32  minfract,
                    PIXA     **ppixarem,
                    NUMA     **pnarem)
{
l_int32    i, j, debug, n, area1, area2;
l_float32  x1, y1, x2, y2, maxval, score, rankscore, threshscore;
NUMA      *nasum, *narem, *nascore;
PIX       *pix1, *pix2;
PIXA      *pixa, *pixarem, *pixad;
PTA       *pta;
L_RECOG   *recog;

    PROCNAME("recogRemoveOutliers");

    if (ppixarem) *ppixarem = NULL;
    if (pnarem) *pnarem = NULL;
    if ((ppixarem && !pnarem) || (!ppixarem && pnarem))
        return (PIXA *)ERROR_PTR("debug output requires both", procName, NULL);
    if (!pixas)
        return (PIXA *)ERROR_PTR("pixas not defined", procName, NULL);
    minscore = L_MIN(minscore, 1.0);
    if (minscore <= 0.0)
        minscore = DEFAULT_MIN_SCORE;
    minfract = L_MIN(minfract, 1.0);
    if (minfract <= 0.0)
        minfract = DEFAULT_MIN_FRACTION;

    debug = (ppixarem) ? 1 : 0;
    if (debug) {
        pixarem = pixaCreate(0);
        *ppixarem = pixarem;
        narem = numaCreate(0);
        *pnarem = narem;
    }

        /* Make a special height-scaled recognizer with average templates */
    recog = recogCreateFromPixa(pixas, 0, 40, 0, 128, 1);
    recogAverageSamples(recog, debug);
    pixad = pixaCreate(0);

    for (i = 0; i < recog->setsize; i++) {
            /* Access the average template and values for scaled
             * images in this class */
        pix1 = pixaGetPix(recog->pixa, i, L_CLONE);
        ptaGetPt(recog->pta, i, &x1, &y1);
        numaGetIValue(recog->nasum, i, &area1);

            /* Get the sorted scores for each sample in the class */
        pixa = pixaaGetPixa(recog->pixaa, i, L_CLONE);
        pta = ptaaGetPta(recog->ptaa, i, L_CLONE);  /* centroids */
        nasum = numaaGetNuma(recog->naasum, i, L_CLONE);  /* fg areas */
        n = pixaGetCount(pixa);
        nascore = numaCreate(n);
        for (j = 0; j < n; j++) {
            pix2 = pixaGetPix(pixa, j, L_CLONE);
            ptaGetPt(pta, j, &x2, &y2);  /* centroid average */
            numaGetIValue(nasum, j, &area2);  /* fg sum average */
            pixCorrelationScoreSimple(pix1, pix2, area1, area2,
                                      x1 - x2, y1 - y2, 5, 5,
                                      recog->sumtab, &score);
            numaAddNumber(nascore, score);
            if (debug && score == 0.0)  /* typ. large size difference */
                fprintf(stderr, "Got 0 score for i = %d, j = %d\n", i, j);
            pixDestroy(&pix2);
        }
        pixDestroy(&pix1);

            /* Find the rankscore, corresonding to the 1.0 - minfract.
             * To maintain the minfract of templates, use as a cutoff
             * the minimum of minscore and the rank score.  Require
             * that at least one template is kept. */
        numaGetRankValue(nascore, 1.0 - minfract, NULL, 0, &rankscore);
        numaGetMax(nascore, &maxval, NULL);
        threshscore = L_MIN(maxval, L_MIN(minscore, rankscore));
        if (debug) {
            L_INFO("minscore = %4.2f, rankscore = %4.2f, threshscore = %4.2f\n",
                   procName, minscore, rankscore, threshscore);
        }

            /* Save the templates that are at or above threshold */
        for (j = 0; j < n; j++) {
            numaGetFValue(nascore, j, &score);
            pix1 = pixaaGetPix(recog->pixaa_u, i, j, L_COPY);
            if (score >= threshscore) {
                pixaAddPix(pixad, pix1, L_INSERT);
            } else if (debug) {
                pixaAddPix(pixarem, pix1, L_INSERT);
                numaAddNumber(narem, score);
            } else {
                pixDestroy(&pix1);
            }
        }
            
        pixaDestroy(&pixa);
        ptaDestroy(&pta);
        numaDestroy(&nasum);
        numaDestroy(&nascore);
    }

    recogDestroy(&recog);
    return pixad;
}


/*------------------------------------------------------------------------*
 *                       Training on unlabeled data                       *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogTrainFromBoot()
 *
 * \param[in]    recogboot  labeled boot recognizer
 * \param[in]    pixas      set of unlabeled input characters
 * \param[in]    minscore   min score for accepting the example; e.g., 0.75
 * \param[in]    threshold  for binarization, if needed
 * \param[in]    debug      1 for debug output saved to recogboot; 0 otherwise
 * \return  pixad   labeled version of input pixas, trained on a BSR,
 *                  or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) This takes %pixas of unscaled single characters and %recboot,
 *          a bootstrep recognizer (BSR) that has been set up with parameters
 *            * scaleh: scale all templates to this height
 *            * linew: width of normalized strokes, or 0 if using
 *              the input image
 *          It modifies the pix in %pixas accordingly and correlates
 *          with the templates in the BSR.  It returns those input
 *          images in %pixas whose best correlation with the BSR is at
 *          or above %minscore.  The returned pix have added text labels
 *          for the text string of the class to which the best
 *          correlated template belongs.
 *      (2) Identification occurs in scaled mode (typically with h = 40),
 *          optionally using a width-normalized line images derived
 *          from those in %pixas.
 * </pre>
 */
PIXA  *
recogTrainFromBoot(L_RECOG   *recogboot,
                   PIXA      *pixas,
                   l_float32  minscore,
                   l_int32    threshold,
                   l_int32    debug)
{
char      *text;
l_int32    i, n, maxdepth, localboot, scaleh, linew;
l_float32  score;
PIX       *pix1, *pix2, *pixdb;
PIXA      *pixa1, *pixa2, *pixa3, *pixad;

    PROCNAME("recogTrainFromBoot");

    if (!recogboot)
        return (PIXA *)ERROR_PTR("recogboot not defined", procName, NULL);
    if (!pixas)
        return (PIXA *)ERROR_PTR("pixas not defined", procName, NULL);

        /* Make sure all input pix are 1 bpp */
    if ((n = pixaGetCount(pixas)) == 0)
        return (PIXA *)ERROR_PTR("no pix in pixa", procName, NULL);
    pixaVerifyDepth(pixas, &maxdepth);
    if (maxdepth == 1) {
        pixa1 = pixaCopy(pixas, L_COPY);
    } else {
        pixa1 = pixaCreate(n);
        for (i = 0; i < n; i++) {
            pix1 = pixaGetPix(pixas, i, L_CLONE);
            pix2 = pixConvertTo1(pix1, threshold);
            pixaAddPix(pixa1, pix2, L_INSERT);
            pixDestroy(&pix1);
        }
    }

        /* Scale the input images to match the BSR */
    scaleh = recogboot->scaleh;
    linew = recogboot->linew;
    pixa2 = pixaCreate(n);
    for (i = 0; i < n; i++) {
        pix1 = pixaGetPix(pixa1, i, L_CLONE);
        pix2 = pixScaleToSize(pix1, 0, scaleh);
        pixaAddPix(pixa2, pix2, L_INSERT);
        pixDestroy(&pix1);
    }
    pixaDestroy(&pixa1);
 
        /* Optionally convert to width-normalized line */
    if (linew > 0)
        pixa3 = pixaSetStrokeWidth(pixa2, linew, 4, 8);
    else
        pixa3 = pixaCopy(pixa2, L_CLONE);
    pixaDestroy(&pixa2);

        /* Identify using recogboot */
    n = pixaGetCount(pixa3); 
    pixad = pixaCreate(n);
    for (i = 0; i < n; i++) {
        pix1 = pixaGetPix(pixa3, i, L_COPY);
        pixSetText(pix1, NULL);  /* remove any existing text or labelling */
        if (!debug) {
            recogIdentifyPix(recogboot, pix1, NULL);
        } else {
            recogIdentifyPix(recogboot, pix1, &pixdb);
            pixaAddPix(recogboot->pixadb_boot, pixdb, L_INSERT);
        }
        rchExtract(recogboot->rch, NULL, &score, &text, NULL, NULL, NULL, NULL);
        if (score >= minscore) {
            pix2 = pixaGetPix(pixas, i, L_COPY);
            pixSetText(pix2, text);
            pixaAddPix(pixad, pix2, L_INSERT); 
            pixaAddPix(recogboot->pixadb_boot, pixdb, L_COPY);
        }
        LEPT_FREE(text);
        pixDestroy(&pix1);
    }
    pixaDestroy(&pixa3);

#if 0
    /* Skip for now; this will be cleaned up/removed in phase 2 */

       /* Generate labeled images for a BAR, using the BSR */
    recog1 = recogCreate(20, 32, L_USE_AVERAGE, threshold, 1);
    for (i = 0; i < n; i++) {
        pix1 = pixaGetPix(pixa1, i, L_COPY);
        pixSetText(pix1, NULL);  /* remove any existing text or labelling */
        recogTrainUnlabeled(recog1, recogboot, pix1, NULL, minscore, debug);
        pixDestroy(&pix1);
    }
    recogTrainingFinished(recog1, 1);
    pixaDestroy(&pixa1);

        /* Now remake the recog based on the input parameters */
    pixa2 = recogExtractPixa(recog1);
    recog2 = recogCreateFromPixa(pixa2, scalew, scaleh, templ_type,
                                 threshold, 1);
    pixaDestroy(&pixa2);

        /* Show what we have, with outliers removed */
    if (debug) {
        lept_mkdir("lept/recog");
        recogDebugAverages(recog2, 1);
        recogShowContent(stderr, recog2, 1, 1);
        recogShowMatchesInRange(recog1, recog2->pixa_tr, minscore, 1.0, 1);
        pixWrite("/tmp/lept/recog/range.png", recog1->pixdb_range, IFF_PNG);
    }

#endif

    return pixad;
}


/*------------------------------------------------------------------------*
 *                     Padding the digit training set                     *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogPadDigitTrainingSet()
 *
 * \param[in/out]   precog   trained; if padding is needed, it is replaced
 *                          by a a new padded recog 
 * \param[in]       scaleh
 * \param[in]       linew
 * \return       0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This is a no-op if padding is not needed.  However,
 *          if it is, this replaces the input recog with a new recog,
 *          padded appropriately with templates from a boot recognizer,
 *          and set up with correlation templates derived from
 *          %scaleh and %linew.
 * </pre>
 */
l_int32
recogPadDigitTrainingSet(L_RECOG  **precog,
                         l_int32    scaleh,
                         l_int32    linew)
{
PIXA     *pixa;
L_RECOG  *recog1, *recog2;
SARRAY   *sa;

    PROCNAME("recogPadDigitTrainingSet");

    if (!precog)
        return ERROR_INT("&recog not defined", procName, 1);
    recog1 = *precog;

    recogIsPaddingNeeded(recog1, &sa);
    if (!sa) return 0;

        /* Get a new pixa with the padding templates added */
    pixa = recogAddDigitPadTemplates(recog1, sa);
    sarrayDestroy(&sa);
    if (!pixa)
        return ERROR_INT("pixa not made", procName, 1);

    recog2 = recogCreateFromPixa(pixa, 0, scaleh, linew, recog1->threshold,
                                 recog1->maxyshift);
    pixaDestroy(&pixa);
    recogDestroy(precog);
    *precog = recog2;
    return 0;
}


/*!
 * \brief   recogIsPaddingNeeded()
 *
 * \param[in]    recog   trained
 * \param[out]   psa     addr of returned string containing text value
 * \return       1 on error; 0 if OK, whether or not additional padding
 *               templates are required.
 *
 * <pre>
 * Notes:
 *      (1) This returns a string array in &sa containing character values
 *          for which extra templates are needed; this sarray is
 *          used by recogGetPadTemplates().  It returns NULL
 *          if no padding templates are needed.
 * </pre>
 */
l_int32
recogIsPaddingNeeded(L_RECOG  *recog,
                     SARRAY  **psa)
{
char      *str;
l_int32    i, nt, min_nopad, nclass, allclasses;
l_float32  minval;
NUMA      *naclass; 
PIXAA     *paa;
SARRAY    *sa;

    PROCNAME("recogIsPaddingNeeded");

    if (!psa)
        return ERROR_INT("&sa not defined", procName, 1);
    *psa = NULL;
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

        /* Do we have samples from all classes? */
    paa = recog->pixaa_u;  /* unscaled bitmaps */
    nclass = pixaaGetCount(recog->pixaa_u, &naclass);  /* unscaled bitmaps */
    allclasses = (nclass == recog->charset_size) ? 1 : 0;

        /* Are there enough samples in each class already? */
    min_nopad = recog->min_nopad;
    numaGetMin(naclass, &minval, NULL);
    if (allclasses && (minval >= min_nopad)) {
        numaDestroy(&naclass);
        return 0;
    }
    
        /* Are any classes not represented? */
    sa = recogAddMissingClassStrings(recog);
    *psa = sa;

        /* Are any other classes under-represented? */
    for (i = 0; i < nclass; i++) {
        numaGetIValue(naclass, i, &nt);
        if (nt < min_nopad) {
            str = sarrayGetString(recog->sa_text, i, L_COPY);
            sarrayAddString(sa, str, L_INSERT);
        }
    }
    numaDestroy(&naclass);
    return 0;
}


/*!
 * \brief   recogAddMissingClassStrings()
 *
 * \param[in]    recog   trained
 * \return       sa  of class string missing in %recog, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) This returns an empty %sa if there is at least one template
 *          in each class in %recog.
 * </pre>
 */
static SARRAY  *
recogAddMissingClassStrings(L_RECOG  *recog)
{
char    *text;
char     str[4];
l_int32  i, nclass, index, ival, n;
NUMA    *na;
PIXAA   *paa;
SARRAY  *sa;

    PROCNAME("recogAddMissingClassStrings");

    if (!recog)
        return (SARRAY *)ERROR_PTR("recog not defined", procName, NULL);

        /* Only handling digits */
    paa = recog->pixaa_u;  /* unscaled bitmaps */
    nclass = pixaaGetCount(recog->pixaa_u, NULL);  /* unscaled bitmaps */
    if (recog->charset_type != 1 || (recog->charset_type == 1 && nclass == 10))
        return sarrayCreate(0);  /* empty */

        /* Make an indicator array for missing classes */
    na = numaCreate(0);
    sa = sarrayCreate(0);
    for (i = 0; i < recog->charset_size; i++)
         numaAddNumber(na, 1);
    for (i = 0; i < nclass; i++) {
        text = sarrayGetString(recog->sa_text, i, L_NOCOPY); 
        index = text[0] - '0';
        numaSetValue(na, index, 0);
    }

        /* Convert to string and add to output */
    for (i = 0; i < nclass; i++) {
        numaGetIValue(na, i, &ival);
        if (ival == 1) {
            str[0] = '0' + i;
            str[1] = '\0';
            sarrayAddString(sa, str, L_COPY);
        }
    }
    numaDestroy(&na);
    return sa;
}

        
/*!
 * \brief   recogAddDigitPadTemplates()
 *
 * \param[in]    recog   trained
 * \param[in]    sa      set of text strings that need to be padded
 * \return  pixa   of all templates from %recog and the additional pad
 *                 templates from a boot recognizer; or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) Call recogIsPaddingNeeded() first, which returns %sa of
 *          template text strings for classes where more templates
 *          are needed.
 * </pre>
 */
PIXA  *
recogAddDigitPadTemplates(L_RECOG  *recog,
                          SARRAY   *sa)
{
char    *str, *text;
l_int32  i, j, n, nt;
PIX     *pix;
PIXA    *pixa1, *pixa2;

    PROCNAME("recogAddDigitPadTemplates");

    if (!recog)
        return (PIXA *)ERROR_PTR("recog not defined", procName, NULL);
    if (!sa)
        return (PIXA *)ERROR_PTR("sa not defined", procName, NULL);
    if (recogCharsetAvailable(recog->charset_type) == FALSE)
        return (PIXA *)ERROR_PTR("boot charset not available", procName, NULL);

        /* Make boot recog templates */
    pixa1 = recogMakeBootDigitTemplates(0);
    n = pixaGetCount(pixa1);

        /* Extract the unscaled templates from %recog */
    pixa2 = recogExtractPixa(recog);

        /* Add selected boot recog templates based on the text strings in sa */
    nt = sarrayGetCount(sa);
    for (i = 0; i < n; i++) {
        pix = pixaGetPix(pixa1, i, L_CLONE);
        text = pixGetText(pix);
        for (j = 0; j < nt; j++) {
            str = sarrayGetString(sa, j, L_NOCOPY);
            if (!strcmp(text, str)) {
                pixaAddPix(pixa2, pix, L_COPY); 
                break;
            }
        }
        pixDestroy(&pix);
    }

    pixaDestroy(&pixa1);
    return pixa2;
}


/*!
 * \brief   recogCharsetAvailable()
 *
 * \param[in]    type of charset for padding
 * \return  1 if available; 0 if not.
 */
static l_int32
recogCharsetAvailable(l_int32  type)
{
l_int32  ret;

    PROCNAME("recogCharsetAvailable");

    switch (type)
    {
    case L_ARABIC_NUMERALS:
        ret = TRUE;
        break;
    case L_LC_ROMAN_NUMERALS:
    case L_UC_ROMAN_NUMERALS:
    case L_LC_ALPHA:
    case L_UC_ALPHA:
        L_INFO("charset type %d not available", procName, type);
        ret = FALSE;
        break;
    default:
        L_INFO("charset type %d is unknown", procName, type);
        ret = FALSE;
        break;
    }

    return ret;
}


/*------------------------------------------------------------------------*
 *                      Making a boot digit recognizer                    *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogMakeBootDigitRecog()
 *
 * \param[in]    scaleh   scale all heights to this; typ. use 40
 * \param[in]    linew    normalized line width; typ. use 5; 0 to skip
 * \param[in]    maxyshift from nominal centroid alignment; typically 0 or 1
 * \param[in]    debug  1 for showing templates; 0 otherwise
 * \return  recog, or NULL on error
 *
 * <pre>
 * Notes:
 *     (1) This takes a set of pre-computed, labeled pixa of single
 *         digits, and generates a recognizer where the character templates
 *         that will be used are derived from the boot-generated pixa:
 *         - extending by replicating the set with different widths,
 *           keeping the height the same
 *         - scaling (isotropically to fixed height)
 *         - optionally generating a skeleton and thickening so that
 *           all strokes have the same width.
 *     (2) The resulting templates are scaled versions of either the
 *         input bitmaps or images with fixed line widths.  To use the
 *         input bitmaps, set %linew = 0; otherwise, set %linew to the
 *         desired line width.
 * </pre>
 */
L_RECOG  *
recogMakeBootDigitRecog(l_int32  scaleh,
                        l_int32  linew,
                        l_int32  maxyshift,
                        l_int32  debug)

{
PIXA     *pixa;
L_RECOG  *recog;

    PROCNAME("recogMakeBootDigitRecog");

        /* Get the templates, extended by horizontal scaling */
    pixa = recogMakeBootDigitTemplates(debug);

        /* Make the boot recog; recogModifyTemplate() will scale the
         * templates and optionally turn them into strokes of fixed width. */
    recog = recogCreateFromPixa(pixa, 0, scaleh, linew, 128, maxyshift);
    pixaDestroy(&pixa);
    if (debug)
        recogShowContent(stderr, recog, 0, 1);

    return recog;
}


/*!
 * \brief   recogMakeBootDigitTemplates()
 *  
 * \param[in]    debug  1 for display of templates
 * \return  pixa   of templates; or NULL on error
 *
 * <pre>
 * Notes:
 *     (1) See recogMakeBootDigitRecog().
 * </pre>
 */
PIXA  *
recogMakeBootDigitTemplates(l_int32  debug)
{
NUMA  *na;
PIX   *pix1, *pix2, *pix3;
PIXA  *pixa1, *pixa2, *pixa3;

    PROCNAME("recogMakeBootDigitTemplates");

    pixa1 = l_bootnum_gen1();
    pixa2 = l_bootnum_gen2();
    pixa3 = l_bootnum_gen3();
    if (debug) {
        pix1 = pixaDisplayTiledWithText(pixa1, 1500, 1.0, 10, 2, 6, 0xff000000);
        pix2 = pixaDisplayTiledWithText(pixa2, 1500, 1.0, 10, 2, 6, 0xff000000);
        pix3 = pixaDisplayTiledWithText(pixa3, 1500, 1.0, 10, 2, 6, 0xff000000);
        pixDisplay(pix1, 0, 0);
        pixDisplay(pix2, 600, 0);
        pixDisplay(pix3, 1200, 0);
        pixDestroy(&pix1);
        pixDestroy(&pix2);
        pixDestroy(&pix3);
    }
    pixaJoin(pixa1, pixa2, 0, -1);
    pixaJoin(pixa1, pixa3, 0, -1);
    pixaDestroy(&pixa2);
    pixaDestroy(&pixa3);

        /* Extend by horizontal scaling */
    na = numaCreate(4);
    numaAddNumber(na, 0.9);
    numaAddNumber(na, 1.1);
    numaAddNumber(na, 1.2);
    pixa2 = pixaExtendByScaling(pixa1, na, L_HORIZ, 1);

    pixaDestroy(&pixa1);
    numaDestroy(&na);
    return pixa2;
}


/*------------------------------------------------------------------------*
 *                               Debugging                                *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogShowContent()
 *
 * \param[in]    fp file  stream
 * \param[in]    recog
 * \param[in]    index    for naming of output files of template images
 * \param[in]    display  1 for showing template images, 0 otherwise
 * \return  0 if OK, 1 on error
 */
l_int32
recogShowContent(FILE     *fp,
                 L_RECOG  *recog,
                 l_int32   index,
                 l_int32   display)
{
char     buf[128];
l_int32  i, val, count;
PIX     *pix;
NUMA    *na;

    PROCNAME("recogShowContent");

    if (!fp)
        return ERROR_INT("stream not defined", procName, 1);
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    fprintf(fp, "Debug print of recog contents\n");
    fprintf(fp, "  Setsize: %d\n", recog->setsize);
    fprintf(fp, "  Binarization threshold: %d\n", recog->threshold);
    fprintf(fp, "  Maximum matching y-jiggle: %d\n", recog->maxyshift);
    if (recog->linew <= 0)
        fprintf(fp, "  Using image templates for matching\n");
    else
        fprintf(fp, "  Using templates with fixed line width for matching\n");
    if (recog->scalew == 0)
        fprintf(fp, "  No width scaling of templates\n");
    else
        fprintf(fp, "  Template width scaled to %d\n", recog->scalew);
    if (recog->scaleh == 0)
        fprintf(fp, "  No height scaling of templates\n");
    else
        fprintf(fp, "  Template height scaled to %d\n", recog->scaleh);
    fprintf(fp, "  Number of samples in each class:\n");
    pixaaGetCount(recog->pixaa_u, &na);
    for (i = 0; i < recog->setsize; i++) {
        l_dnaGetIValue(recog->dna_tochar, i, &val);
        numaGetIValue(na, i, &count);
        if (val < 128)
            fprintf(fp, "    class %d, char %c:   %d\n", i, val, count);
        else
            fprintf(fp, "    class %d, val %d:   %d\n", i, val, count);
    }
    numaDestroy(&na);

    if (display) {
        lept_mkdir("lept/recog");
        pix = pixaaDisplayByPixa(recog->pixaa_u, 20, 20, 1000);
        snprintf(buf, sizeof(buf), "/tmp/lept/recog/templates_u.%d.png", index);
        pixWrite(buf, pix, IFF_PNG); 
        pixDisplay(pix, 0, 200 * index);
        pixDestroy(&pix);
        if (recog->train_done) {
            pix = pixaaDisplayByPixa(recog->pixaa, 20, 20, 1000);
            snprintf(buf, sizeof(buf),
                     "/tmp/lept/recog/templates.%d.png", index);
            pixWrite(buf, pix, IFF_PNG); 
            pixDisplay(pix, 800, 200 * index);
            pixDestroy(&pix);
        }
    }
    return 0;
}


/*!
 * \brief   recogDebugAverages()
 *
 * \param[in]    recog
 * \param[in]    debug 0 no output; 1 for images; 2 for text; 3 for both
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) Generates an image that pairs each of the input images used
 *          in training with the average template that it is best
 *          correlated to.  This is written into the recog.
 *      (2) It also generates pixa_tr of all the input training images,
 *          which can be used, e.g., in recogShowMatchesInRange().
 * </pre>
 */
l_int32
recogDebugAverages(L_RECOG  *recog,
                   l_int32   debug)
{
l_int32    i, j, n, np, index;
l_float32  score;
PIX       *pix1, *pix2, *pix3;
PIXA      *pixa, *pixat;
PIXAA     *paa1, *paa2;

    PROCNAME("recogDebugAverages");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

        /* Mark the training as finished if necessary, and make sure
         * that the average templates have been built. */
    recogAverageSamples(recog, 0);
    paa1 = recog->pixaa;

        /* Save a pixa of all the training examples */
    if (!recog->pixa_tr)
        recog->pixa_tr = pixaaFlattenToPixa(paa1, NULL, L_CLONE);

        /* Destroy any existing image and make a new one */
    if (recog->pixdb_ave)
        pixDestroy(&recog->pixdb_ave);
    n = pixaaGetCount(paa1, NULL);
    paa2 = pixaaCreate(n);
    for (i = 0; i < n; i++) {
        pixa = pixaCreate(0);
        pixat = pixaaGetPixa(paa1, i, L_CLONE);
        np = pixaGetCount(pixat);
        for (j = 0; j < np; j++) {
            pix1 = pixaaGetPix(paa1, i, j, L_CLONE);
            recogIdentifyPix(recog, pix1, &pix2);
            rchExtract(recog->rch, &index, &score, NULL, NULL, NULL,
                       NULL, NULL);
            if (debug >= 2)
                fprintf(stderr, "index = %d, score = %7.3f\n", index, score);
            pix3 = pixAddBorder(pix2, 2, 1);
            pixaAddPix(pixa, pix3, L_INSERT);
            pixDestroy(&pix1);
            pixDestroy(&pix2);
        }
        pixaaAddPixa(paa2, pixa, L_INSERT);
        pixaDestroy(&pixat);
    }
    recog->pixdb_ave = pixaaDisplayByPixa(paa2, 20, 20, 2500);
    if (debug % 2) {
        lept_mkdir("lept/recog");
        pixWrite("/tmp/lept/recog/templ_match.png", recog->pixdb_ave, IFF_PNG);
        pixDisplay(recog->pixdb_ave, 100, 100);
    }

    pixaaDestroy(&paa2);
    return 0;
}


/*!
 * \brief   recogShowAverageTemplates()
 *
 * \param[in]    recog
 * \return  0 on success, 1 on failure
 *
 * <pre>
 * Notes:
 *      (1) This debug routine generates a display of the averaged templates,
 *          both scaled and unscaled, with the centroid visible in red.
 * </pre>
 */
l_int32
recogShowAverageTemplates(L_RECOG  *recog)
{
l_int32    i, size;
l_float32  x, y;
PIX       *pix1, *pix2, *pixr;
PIXA      *pixat, *pixadb;

    PROCNAME("recogShowAverageTemplates");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    fprintf(stderr, "minwidth_u = %d, minheight_u = %d, maxheight_u = %d\n",
            recog->minwidth_u, recog->minheight_u, recog->maxheight_u);
    fprintf(stderr, "minw = %d, minh = %d, maxh = %d\n",
            recog->min_splitw, recog->min_splith, recog->max_splith);

    pixaDestroy(&recog->pixadb_ave);

    pixr = pixCreate(3, 3, 32);  /* 3x3 red square for centroid location */
    pixSetAllArbitrary(pixr, 0xff000000);
    pixadb = pixaCreate(2);

        /* Unscaled bitmaps */
    size = recog->setsize;
    pixat = pixaCreate(size);
    for (i = 0; i < size; i++) {
        if ((pix1 = pixaGetPix(recog->pixa_u, i, L_CLONE)) == NULL)
            continue;
        pix2 = pixConvertTo32(pix1);
        ptaGetPt(recog->pta_u, i, &x, &y);
        pixRasterop(pix2, (l_int32)(x - 0.5), (l_int32)(y - 0.5), 3, 3,
                    PIX_SRC, pixr, 0, 0);
        pixaAddPix(pixat, pix2, L_INSERT);
        pixDestroy(&pix1);
    }
    pix1 = pixaDisplayTiledInRows(pixat, 32, 3000, 1.0, 0, 20, 0);
    pixaAddPix(pixadb, pix1, L_INSERT);
    pixDisplay(pix1, 100, 100);
    pixaDestroy(&pixat);

        /* Scaled bitmaps */
    pixat = pixaCreate(size);
    for (i = 0; i < size; i++) {
        if ((pix1 = pixaGetPix(recog->pixa, i, L_CLONE)) == NULL)
            continue;
        pix2 = pixConvertTo32(pix1);
        ptaGetPt(recog->pta, i, &x, &y);
        pixRasterop(pix2, (l_int32)(x - 0.5), (l_int32)(y - 0.5), 3, 3,
                    PIX_SRC, pixr, 0, 0);
        pixaAddPix(pixat, pix2, L_INSERT);
        pixDestroy(&pix1);
    }
    pix1 = pixaDisplayTiledInRows(pixat, 32, 3000, 1.0, 0, 20, 0);
    pixaAddPix(pixadb, pix1, L_INSERT);
    pixDisplay(pix1, 100, 100);
    pixaDestroy(&pixat);
    pixDestroy(&pixr);
    recog->pixadb_ave = pixadb;
    return 0;
}


/*!
 * \brief   recogDisplayOutliers()
 *
 * \param[in]    pixas    unscaled labeled templates
 * \param[in]    nas      scores of templates (against class averages)
 * \return  pix    tiled pixa with text and scores, or NULL on failure
 * <pre>
 * Notes:
 *      (1) This debug routine is called after recogRemoveOutliers(),
 *          and takes the removed templates and their scores as input.
 * </pre>
 */
PIX  *
recogDisplayOutliers(PIXA  *pixas,
                     NUMA  *nas)
{
char      *text;
char       buf[16];
l_int32    i, n;
l_float32  fval;
PIX       *pix1, *pix2;
PIXA      *pixa1;

    PROCNAME("recogDisplayOutliers");

    if (!pixas)
        return (PIX *)ERROR_PTR("pixas not defined", procName, NULL);
    if (!nas)
        return (PIX *)ERROR_PTR("nas not defined", procName, NULL);
    n = pixaGetCount(pixas);
    if (numaGetCount(nas) != n)
        return (PIX *)ERROR_PTR("pixas and nas sizes differ", procName, NULL);

    pixa1 = pixaCreate(n);
    for (i = 0; i < n; i++) {
        pix1 = pixaGetPix(pixas, i, L_CLONE);
        pix2 = pixAddBlackOrWhiteBorder(pix1, 25, 25, 0, 0, L_GET_WHITE_VAL);
        text = pixGetText(pix1);
        numaGetFValue(nas, i, &fval);
        snprintf(buf, sizeof(buf), "'%s': %5.2f", text, fval);
        pixSetText(pix2, buf);
        pixaAddPix(pixa1, pix2, L_INSERT);
        pixDestroy(&pix1);
    }
    pix1 = pixaDisplayTiledWithText(pixa1, 1500, 1.0, 20, 2, 6, 0xff000000);
    pixaDestroy(&pixa1);
    return pix1;
}


/*!
 * \brief   recogShowMatchesInRange()
 *
 * \param[in]    recog
 * \param[in]    pixa of 1 bpp images to match
 * \param[in]    minscore, maxscore range to include output
 * \param[in]    display to display the result
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This gives a visual output of the best matches for a given
 *          range of scores.  Each pair of images can optionally be
 *          labeled with the index of the best match and the correlation.
 *      (2) To use this, save a set of 1 bpp images (labeled or
 *          unlabeled) that can be given to a recognizer in a pixa.
 *          Then call this function with the pixa and parameters
 *          to filter a range of scores.
 * </pre>
 */
l_int32
recogShowMatchesInRange(L_RECOG     *recog,
                        PIXA        *pixa,
                        l_float32    minscore,
                        l_float32    maxscore,
                        l_int32      display)
{
l_int32    i, n, index, depth;
l_float32  score;
NUMA      *nascore, *naindex;
PIX       *pix1, *pix2;
PIXA      *pixa1, *pixa2;

    PROCNAME("recogShowMatchesInRange");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!pixa)
        return ERROR_INT("pixa not defined", procName, 1);

        /* Run the recognizer on the set of images */
    n = pixaGetCount(pixa);
    nascore = numaCreate(n);
    naindex = numaCreate(n);
    pixa1 = pixaCreate(n);
    for (i = 0; i < n; i++) {
        pix1 = pixaGetPix(pixa, i, L_CLONE);
        recogIdentifyPix(recog, pix1, &pix2);
        rchExtract(recog->rch, &index, &score, NULL, NULL, NULL, NULL, NULL);
        numaAddNumber(nascore, score);
        numaAddNumber(naindex, index);
        pixaAddPix(pixa1, pix2, L_INSERT);
        pixDestroy(&pix1);
    }

        /* Filter the set and optionally add text to each */
    pixa2 = pixaCreate(n);
    depth = 1;
    for (i = 0; i < n; i++) {
        numaGetFValue(nascore, i, &score);
        if (score < minscore || score > maxscore) continue;
        pix1 = pixaGetPix(pixa1, i, L_CLONE);
        numaGetIValue(naindex, i, &index);
        pix2 = recogShowMatch(recog, pix1, NULL, NULL, index, score);
        if (i == 0) depth = pixGetDepth(pix2);
        pixaAddPix(pixa2, pix2, L_INSERT);
        pixDestroy(&pix1);
    }

        /* Package it up */
    pixDestroy(&recog->pixdb_range);
    if (pixaGetCount(pixa2) > 0) {
        recog->pixdb_range =
            pixaDisplayTiledInRows(pixa2, depth, 2500, 1.0, 0, 20, 1);
        if (display)
            pixDisplay(recog->pixdb_range, 300, 100);
    } else {
        L_INFO("no character matches in the range of scores\n", procName);
    }

    pixaDestroy(&pixa1);
    pixaDestroy(&pixa2);
    numaDestroy(&nascore);
    numaDestroy(&naindex);
    return 0;
}


/*!
 * \brief   recogShowMatch()
 *
 * \param[in]    recog
 * \param[in]    pix1  input pix; several possibilities
 * \param[in]    pix2  [optional] matching template
 * \param[in]    box  [optional] region in pix1 for which pix2 matches
 * \param[in]    index  index of matching template; use -1 to disable printing
 * \param[in]    score  score of match
 * \return  pixd pair of images, showing input pix and best template,
 *                    optionally with matching information, or NULL on error.
 *
 * <pre>
 * Notes:
 *      (1) pix1 can be one of these:
 *          (a) The input pix alone, which can be either a single character
 *              (box == NULL) or several characters that need to be
 *              segmented.  If more than character is present, the box
 *              region is displayed with an outline.
 *          (b) Both the input pix and the matching template.  In this case,
 *              pix2 and box will both be null.
 *      (2) If the bmf has been made (by a call to recogMakeBmf())
 *          and the index \>= 0, the text field, match score and index
 *          will be rendered; otherwise their values will be ignored.
 * </pre>
 */
PIX *
recogShowMatch(L_RECOG   *recog,
               PIX       *pix1,
               PIX       *pix2,
               BOX       *box,
               l_int32    index,
               l_float32  score)
{
char    buf[32];
char   *text;
L_BMF  *bmf;
PIX    *pix3, *pix4, *pix5, *pixd;
PIXA   *pixa;

    PROCNAME("recogShowMatch");

    if (!recog)
        return (PIX *)ERROR_PTR("recog not defined", procName, NULL);
    if (!pix1)
        return (PIX *)ERROR_PTR("pix1 not defined", procName, NULL);

    bmf = (recog->bmf && index >= 0) ? recog->bmf : NULL;
    if (!pix2 && !box && !bmf)  /* nothing to do */
        return pixCopy(NULL, pix1);

    pix3 = pixConvertTo32(pix1);
    if (box)
        pixRenderBoxArb(pix3, box, 1, 255, 0, 0);

    if (pix2) {
        pixa = pixaCreate(2);
        pixaAddPix(pixa, pix3, L_CLONE);
        pixaAddPix(pixa, pix2, L_CLONE);
        pix4 = pixaDisplayTiledInRows(pixa, 1, 500, 1.0, 0, 15, 0);
        pixaDestroy(&pixa);
    } else {
        pix4 = pixCopy(NULL, pix3);
    }
    pixDestroy(&pix3);

    if (bmf) {
        pix5 = pixAddBorderGeneral(pix4, 55, 55, 0, 0, 0xffffff00);
        recogGetClassString(recog, index, &text);
        snprintf(buf, sizeof(buf), "C=%s, S=%4.3f, I=%d", text, score, index);
        pixd = pixAddSingleTextblock(pix5, bmf, buf, 0xff000000,
                                     L_ADD_BELOW, NULL);
        pixDestroy(&pix5);
        LEPT_FREE(text);
    } else {
        pixd = pixClone(pix4);
    }
    pixDestroy(&pix4);

    return pixd;
}


/*------------------------------------------------------------------------*
 *                             Static helper                              *
 *------------------------------------------------------------------------*/
static char *
l_charToString(char byte)
{
char  *str;

  str = (char *)LEPT_CALLOC(2, sizeof(char));
  str[0] = byte;
  return str;
}
