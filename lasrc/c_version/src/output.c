/*****************************************************************************
FILE: output.c
  
PURPOSE: Contains functions for handling of the output data files for this
application.

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

LICENSE TYPE:  NASA Open Source Agreement Version 1.3

NOTES:
*****************************************************************************/

#include <time.h>
#include <ctype.h>
#include "output.h"

/******************************************************************************
MODULE:  open_output

PURPOSE:  Set up the output data structure.  Open the output file for write
and read access.

RETURN VALUE:
Type = Output_t
Value          Description
-----          -----------
NULL           Error occurred opening the file
not-NULL       Successful completion

NOTES:
******************************************************************************/
Output_t *open_output
(
    Espa_internal_meta_t *in_meta,  /* I: input metadata structure */
    Input_t *input,                 /* I: input band data structure */
    Myoutput_t output_type          /* I: are we processing TOA, SR, RADSAT
                                          outputs? */
)
{
    char FUNC_NAME[] = "open_output";   /* function name */
    char errmsg[STR_SIZE];       /* error message */
    char *upper_str = NULL;      /* upper case version of the SI short name */
    char scene_name[STR_SIZE];   /* scene name for the current scene */
    char production_date[MAX_DATE_LEN+1]; /* current date/time for production */
    time_t tp;                   /* time structure */
    struct tm *tm = NULL;        /* time structure for UTC time */
    int nband = 0;               /* number of output bands to be created */
    int ib;                      /* looping variable for bands */
    int refl_indx = -1;          /* band index in XML file for the reflectance
                                    band */
    Output_t *output = NULL;     /* output data structure to be returned */
    Espa_band_meta_t *bmeta = NULL;  /* pointer to the band metadata array
                                        within the output structure */
    char s2_bandname[][3] = {"1", "2", "3", "4", "5", "6", "7", "8", "8a", "9",
                             "10", "11", "12"};

    /* Create the Output data structure */
    output = (Output_t *) malloc (sizeof (Output_t));
    if (output == NULL) 
    {
        sprintf (errmsg, "Error allocating Output data structure");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
  
    /* Use band 1 band-related metadata for the reflectance information for
       Landsat (Level 1 products).  Use band 2 band-related metadata for the
       reflectance information (Level-1 products).  Sentinel-2 bands have
       different resolutions and we want a band which represents the 10m
       resolution. */
    for (ib = 0; ib < in_meta->nbands; ib++)
    {
        if ((!strcmp (in_meta->band[ib].name, "b1") &&
             !strncmp (in_meta->band[ib].product, "L1", 2)) ||
            (!strcmp (in_meta->band[ib].name, "B01") &&
             !strcmp (in_meta->band[ib].product, "MSIL1C")))
        {
            /* this is the index we'll use for reflectance band info */
            refl_indx = ib;
            break;
        }
    }

    /* Make sure we found the Level-1 band 1 */
    if (refl_indx == -1)
    {
        sprintf (errmsg, "Unable to find the Level-1 band 1 bands in the XML "
            "file for initializing the output metadata.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }

    /* Initialize the internal metadata for the output product. The global
       metadata won't be updated, however the band metadata will be updated
       and used later for appending to the original XML file. */
    init_metadata_struct (&output->metadata);

    /* Copy the instrument type */
    output->inst = input->meta.inst;

    /* Allocate memory for the total bands; radsat is only one band */
    if (output_type == OUTPUT_RADSAT)
        nband = 1;
    else
    {
        if (input->meta.sat == SAT_LANDSAT_8)
            nband = NBAND_L8_TTL_OUT;
        else if (input->meta.sat == SAT_SENTINEL_2)
            nband = NBAND_S2_TTL_OUT;
    }
    if (allocate_band_metadata (&output->metadata, nband) != SUCCESS)
    {
        sprintf (errmsg, "Allocating band metadata.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
    bmeta = output->metadata.band;

    /* Pull the scene name from the metadata */
    strcpy (scene_name, in_meta->global.product_id);
  
    /* Get the current date/time (UTC) for the production date of each band */
    if (time (&tp) == -1)
    {
        sprintf (errmsg, "Unable to obtain the current time.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
  
    tm = gmtime (&tp);
    if (tm == NULL)
    {
        sprintf (errmsg, "Converting time to UTC.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
  
    if (strftime (production_date, MAX_DATE_LEN, "%Y-%m-%dT%H:%M:%SZ", tm) == 0)
    {
        sprintf (errmsg, "Formatting the production date/time.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }

    /* Populate the data structure, using information from the reflectance
       bands */
    output->open = false;
    output->nband = nband;
    output->nlines = input->size.nlines;
    output->nsamps = input->size.nsamps;
    for (ib = 0; ib < output->nband; ib++)
        output->fp_bin[ib] = NULL;
 
    for (ib = 0; ib < nband; ib++)
    {
        strncpy (bmeta[ib].short_name, in_meta->band[refl_indx].short_name, 4);
        bmeta[ib].short_name[4] = '\0';
        if (output_type == OUTPUT_TOA)
        {  /* Only applies to the L8 product */
            if ((ib == SR_L8_BAND10) || (ib == SR_L8_BAND11))
            {
                strcat (bmeta[ib].short_name, "BT");
                strcpy (bmeta[ib].product, "toa_bt");
            }
            else
            {
                strcat (bmeta[ib].short_name, "TOA");
                strcpy (bmeta[ib].product, "toa_refl");
            }
        }
        else if (output_type == OUTPUT_SR)
        {
            strcat (bmeta[ib].short_name, "SR");
            strcpy (bmeta[ib].product, "sr_refl");
        }
        else if (output_type == OUTPUT_RADSAT)
        {
            strcat (bmeta[ib].short_name, "RADSAT");
            strcpy (bmeta[ib].product, "toa_refl");
            strcpy (bmeta[ib].source, "level1");
        }

        bmeta[ib].nlines = output->nlines;
        bmeta[ib].nsamps = output->nsamps;
        bmeta[ib].pixel_size[0] = input->size.pixsize[0];
        bmeta[ib].pixel_size[1] = input->size.pixsize[1];
        strcpy (bmeta[ib].pixel_units, "meters");
        sprintf (bmeta[ib].app_version, "LaSRC_%s",
            SR_VERSION);
        strcpy (bmeta[ib].production_date, production_date);

        /* Handle the aerosol band differently.  If this is only TOA then we
           don't need to process the aerosol mask.  If this is SR, then we
           don't need to process the cirrus or thermal bands.  If this is
           RADSAT then we only have one band. */
        if ((output_type == OUTPUT_TOA) && (ib == SR_L8_AEROSOL))
            continue;
        else if ((output_type == OUTPUT_SR) &&
                 (input->meta.sat == SAT_LANDSAT_8) &&
                 ((ib == SR_L8_BAND9) || (ib == SR_L8_BAND10) ||
                  (ib == SR_L8_BAND11)))
            continue;
        else if ((output_type == OUTPUT_SR) && 
                 (((input->meta.sat == SAT_LANDSAT_8) &&
                  (ib == SR_L8_AEROSOL)) ||
                  ((input->meta.sat == SAT_SENTINEL_2) &&
                  (ib == SR_S2_AEROSOL))))
        {
            /* Common QA band fields */
            bmeta[ib].data_type = ESPA_UINT8;
            bmeta[ib].fill_value = (1 << IPFLAG_FILL);
            strcpy (bmeta[ib].name, "sr_aerosol");
            strcpy (bmeta[ib].long_name, "surface reflectance aerosol mask");
            strcpy (bmeta[ib].category, "qa");
            strcpy (bmeta[ib].data_units, "quality/feature classification");
    
            /* Set up aerosol bitmap information */
            if (allocate_bitmap_metadata (&bmeta[ib], 8) != SUCCESS)
            {
                sprintf (errmsg, "Allocating aerosol bitmap.");
                error_handler (true, FUNC_NAME, errmsg);
                return (NULL);
            }

            /* Identify the bitmap values for the mask, knowing there are
               differences between Landsat-8 and Sentinel-2 */
            if (input->meta.sat == SAT_LANDSAT_8)
            {
                strcpy (bmeta[ib].bitmap_description[0], "fill");
                strcpy (bmeta[ib].bitmap_description[1],
                    "valid aerosol retrieval (center pixel of NxN window)");
                strcpy (bmeta[ib].bitmap_description[2], "water pixel (or "
                    "water pixel was used in the fill-the-window "
                    "interpolation)");
                strcpy (bmeta[ib].bitmap_description[3], "cloud or cirrus");
                strcpy (bmeta[ib].bitmap_description[4], "cloud shadow");
                strcpy (bmeta[ib].bitmap_description[5], "non-center window "
                    "pixel for which aerosol was interpolated from surrounding "
                    "NxN center pixels");
                strcpy (bmeta[ib].bitmap_description[6], "aerosol level");
                strcpy (bmeta[ib].bitmap_description[7], "aerosol level");
            }
            else if (input->meta.sat == SAT_SENTINEL_2)
            {
                strcpy (bmeta[ib].bitmap_description[0], "fill");
                strcpy (bmeta[ib].bitmap_description[1], "clear land pixel");
                strcpy (bmeta[ib].bitmap_description[2], "aerosol failed");
                strcpy (bmeta[ib].bitmap_description[3], "not used");
                strcpy (bmeta[ib].bitmap_description[4], "not used");
                strcpy (bmeta[ib].bitmap_description[5],
                    "aerosol value was interpolated from surrounding window "
                    "aerosols");
                strcpy (bmeta[ib].bitmap_description[6], "aerosol level");
                strcpy (bmeta[ib].bitmap_description[7], "aerosol level");
            }

            strncpy (bmeta[ib].short_name,
                in_meta->band[refl_indx].short_name, 4);
            bmeta[ib].short_name[4] = '\0';
            strcat (bmeta[ib].short_name, "AERO");
        }
        else if (output_type == OUTPUT_RADSAT)
        {
            /* Common QA band fields */
            bmeta[ib].data_type = ESPA_UINT16;
            bmeta[ib].fill_value = RADSAT_FILL_VALUE;
            strcpy (bmeta[ib].name, "radsat_qa");
            strcpy (bmeta[ib].long_name, "saturation mask");
            strcpy (bmeta[ib].category, "qa");
            strcpy (bmeta[ib].data_units, "bitmap");

            /* Set up radsat bitmap information */
            if (allocate_bitmap_metadata (&bmeta[ib], 12) != SUCCESS)
            {
                sprintf (errmsg, "Allocating radsat bitmap.");
                error_handler (true, FUNC_NAME, errmsg);
                return (NULL);
            }

            /* Identify the bitmap values for the mask */
            strcpy (bmeta[ib].bitmap_description[0],
                "Data Fill Flag (0 = valid data, 1 = invalid data)");
            strcpy (bmeta[ib].bitmap_description[1],
                "Band 1 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[2],
                "Band 2 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[3],
                "Band 3 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[4],
                "Band 4 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[5],
                "Band 5 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[6],
                "Band 6 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[7],
                "Band 7 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[8], "N/A");
            strcpy (bmeta[ib].bitmap_description[9],
                "Band 9 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[10],
                "Band 10 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
            strcpy (bmeta[ib].bitmap_description[11],
                "Band 11 Data Saturation Flag (0 = valid data, "
                    "1 = saturated data)");
        }
        else
        {
            bmeta[ib].data_type = ESPA_INT16;
            bmeta[ib].fill_value = FILL_VALUE;
            strcpy (bmeta[ib].category, "image");
            strcpy (bmeta[ib].data_units, "reflectance");

            if ((input->meta.sat == SAT_LANDSAT_8) &&
                (ib == SR_L8_BAND10 || ib == SR_L8_BAND11))
            {  /* L8 thermal bands */
                bmeta[ib].scale_factor = SCALE_FACTOR_TH;
                bmeta[ib].valid_range[0] = (float) MIN_VALID_TH;
                bmeta[ib].valid_range[1] = (float) MAX_VALID_TH;
            }
            else
            {  /* reflectance bands */
                bmeta[ib].scale_factor = SCALE_FACTOR;
                bmeta[ib].valid_range[0] = (float) MIN_VALID;
                bmeta[ib].valid_range[1] = (float) MAX_VALID;
            }

            if ((input->meta.sat == SAT_LANDSAT_8) &&
                (ib >= SR_L8_BAND1 && ib <= SR_L8_BAND7))
            {  /* L8 reflectance bands */
                if (output_type == OUTPUT_TOA)
                {
                    sprintf (bmeta[ib].name, "toa_band%d", ib+1);
                    sprintf (bmeta[ib].long_name, "band %d top-of-atmosphere "
                        "reflectance", ib+1);
                }
                else if (output_type == OUTPUT_SR)
                {
                    sprintf (bmeta[ib].name, "sr_band%d", ib+1);
                    sprintf (bmeta[ib].long_name, "band %d surface reflectance",
                        ib+1);
                }
            }
            else if ((input->meta.sat == SAT_SENTINEL_2) &&
                (ib >= SR_S2_BAND1 && ib <= SR_S2_BAND12))
            {  /* S2 reflectance bands */
                if (output_type == OUTPUT_SR)
                {
                    sprintf (bmeta[ib].name, "sr_band%s", s2_bandname[ib]);
                    sprintf (bmeta[ib].long_name, "band %s surface reflectance",
                        s2_bandname[ib]);
                }
            }
            else if ((input->meta.sat == SAT_LANDSAT_8) && (ib == SR_L8_BAND9))
            {  /* L8 cirrus band; band 9 is only atmospherically corrected */
                sprintf (bmeta[ib].name, "toa_band%d", ib+2);
                sprintf (bmeta[ib].long_name, "band %d top-of-atmosphere "
                    "reflectance", ib+2);
            }
            else if ((input->meta.sat == SAT_LANDSAT_8) &&
                (ib == SR_L8_BAND10 || ib == SR_L8_BAND11))
            {  /* L8 thermal bands */
                sprintf (bmeta[ib].name, "bt_band%d", ib+2);
                sprintf (bmeta[ib].long_name, "band %d top-of-atmosphere "
                    "brightness temperature", ib+2);
                sprintf (bmeta[ib].data_units, "temperature (kelvin)");
            }
        }

        /* Set up the filename with the scene name and band name and open the
           file for read/write access.  Don't open if this is OLI-only and
           these are the thermal bands. */
        if (((input->meta.sat == SAT_LANDSAT_8) &&
            (ib != SR_L8_BAND10 && ib != SR_L8_BAND11)) ||
             output->inst != INST_OLI)
        {
            sprintf (bmeta[ib].file_name, "%s_%s.img", scene_name,
                bmeta[ib].name);
            output->fp_bin[ib] = open_raw_binary (bmeta[ib].file_name, "w+");
            if (output->fp_bin[ib] == NULL)
            {
                sprintf (errmsg, "Unable to open output band %d file: %s", ib,
                    bmeta[ib].file_name);
                error_handler (true, FUNC_NAME, errmsg);
                return (NULL);
            }
        }

        /* Free the memory for the upper-case string */
        free (upper_str);
    }  /* for ib */
    output->open = true;

    /* Successful completion */
    return output;
}


/******************************************************************************
MODULE:  close_output

PURPOSE:  Closes the output files

RETURN VALUE:
Type = int
Value      Description
-----      -----------
ERROR      Error occurred closing the output files
SUCCESS    Successful completion

NOTES:
******************************************************************************/
int close_output
(
    Sat_t sat,              /* I: satellite */
    Output_t *output,       /* I/O: Output data structure to close */
    Myoutput_t output_type  /* I: are we processing TOA, SR, RADSAT outputs? */
)
{
    char FUNC_NAME[] = "close_output";   /* function name */
    char errmsg[STR_SIZE];    /* error message */
    int ib;                   /* looping variable */

    if (!output->open)
    {
        sprintf (errmsg, "File is not open, so it cannot be closed.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    /* Close raw binary products */
    for (ib = 0; ib < output->nband; ib++)
    {
        /* No aerosol band with the output L8 TOA product */
        if ((output_type == OUTPUT_TOA) && (sat == SAT_LANDSAT_8) &&
            (ib == SR_L8_AEROSOL))
            continue;
        else if ((sat == SAT_LANDSAT_8) && (output_type == OUTPUT_SR) &&
            ((ib == SR_L8_BAND9) || (ib == SR_L8_BAND10) ||
             (ib == SR_L8_BAND11)))
            /* No cirrus or thermal with the output L8 SR products */
            continue;
        else
        {
            /* No thermal bands are open for OLI-only scenes */
            if ((sat == SAT_LANDSAT_8) &&
                 ((ib != SR_L8_BAND10 && ib != SR_L8_BAND11) ||
                  output->inst != INST_OLI))
                close_raw_binary (output->fp_bin[ib]);
        }
    }
    output->open = false;

    return (SUCCESS);
}


/******************************************************************************
MODULE:  free_output

PURPOSE:  Frees the memory for the output data structure

RETURN VALUE:
Type = int
Value      Description
-----      -----------
ERROR      Error occurred freeing the data structure
SUCCESS    Successful completion

NOTES:
******************************************************************************/
int free_output
(
    Output_t *output,       /* I/O: Output data structure to free */
    Myoutput_t output_type  /* I: are we processing TOA, SR, RADSAT outputs? */
)
{
    char FUNC_NAME[] = "free_output";   /* function name */
    char errmsg[STR_SIZE];    /* error message */
    int b;                    /* looping variable for the bits */
  
    if (output->open) 
    {
        sprintf (errmsg, "File is still open, so cannot free memory.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
  
    if (output != NULL)
    {
        /* Free the bitmap data for the L8 aerosol and radsat bands */
        if (output_type == OUTPUT_SR &&
            output->metadata.band[SR_L8_AEROSOL].nbits > 0)
        {
            for (b = 0; b < output->metadata.band[SR_L8_AEROSOL].nbits; b++)
               free(output->metadata.band[SR_L8_AEROSOL].bitmap_description[b]);
            free (output->metadata.band[SR_L8_AEROSOL].bitmap_description);
        }

        if (output_type == OUTPUT_RADSAT &&
            output->metadata.band[SR_RADSAT].nbits > 0)
        {
            for (b = 0; b < output->metadata.band[SR_RADSAT].nbits; b++)
                free (output->metadata.band[SR_RADSAT].bitmap_description[b]);
            free (output->metadata.band[SR_RADSAT].bitmap_description);
        }

        /* Free the bitmap data for the S2 aerosol band */
        if (output_type == OUTPUT_SR &&
            output->metadata.nbands == NBAND_S2_TTL_OUT)
        {
            for (b = 0; b < output->metadata.band[SR_S2_AEROSOL].nbits; b++)
               free(output->metadata.band[SR_S2_AEROSOL].bitmap_description[b]);
            free (output->metadata.band[SR_S2_AEROSOL].bitmap_description);
        }

        /* Free the band data */
        free (output->metadata.band);

        /* Free the data structure */
        free (output);
    }
  
    return (SUCCESS);
}


/******************************************************************************
MODULE:  put_output_lines

PURPOSE:  Writes a line or lines of data to the output file.

RETURN VALUE:
Type = int
Value      Description
-----      -----------
ERROR      Error occurred writing the output data
SUCCESS    Successful completion

NOTES:
******************************************************************************/
int put_output_lines
(
    Output_t *output,  /* I: output data structure; buf contains the line to
                             be written */
    void *buf,         /* I: buffer to be written */
    int iband,         /* I: current band to be written (0-based) */
    int iline,         /* I: current line to be written (0-based) */
    int nlines,        /* I: number of lines to be written */
    int nbytes         /* I: number of bytes per pixel in this band */
)
{
    char FUNC_NAME[] = "put_output_lines";   /* function name */
    char errmsg[STR_SIZE];    /* error message */
    long loc;                 /* current location in the output file */
  
    /* Check the parameters */
    if (output == (Output_t *)NULL) 
    {
        sprintf (errmsg, "Invalid input structure");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (!output->open)
    {
        sprintf (errmsg, "File is not open.  Cannot write data.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (iband < 0 || iband >= output->nband)
    {
        sprintf (errmsg, "Invalid band number.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (iline < 0 || iline >= output->nlines)
    {
        sprintf (errmsg, "Invalid line number.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (nlines < 0 || iline+nlines > output->nlines)
    {
        sprintf (errmsg, "Line plus number of lines to be written exceeds "
            "the predefined size of the image.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
  
    /* Write the data, but first seek to the correct line */
    loc = (long) iline * output->nsamps * nbytes;
    if (fseek (output->fp_bin[iband], loc, SEEK_SET))
    {
        sprintf (errmsg, "Seeking to the current line in the output file for "
            "band %d", iband);
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    if (write_raw_binary (output->fp_bin[iband], nlines, output->nsamps,
        nbytes, buf) != SUCCESS)
    {
        sprintf (errmsg, "Error writing the output line(s) for band %d.",
            iband);
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    
    return (SUCCESS);
}


/******************************************************************************
MODULE:  upper_case_str

PURPOSE:  Returns the upper case version of the input string.

RETURN VALUE:
Type = char *
Value      Description
-----      -----------
up_str     Upper case version of the input string

NOTES:
******************************************************************************/
char *upper_case_str
(
    char *str    /* I: string to be converted to upper case */
)
{
    char *up_str = NULL;    /* upper case version of the input string */
    char *ptr = NULL;       /* pointer to the upper case string */

    up_str = strdup (str);
    ptr = up_str;
    while (*ptr != '\0')
    {
        if (islower (*ptr))
            *ptr = toupper (*ptr);
        ptr++;
    }

    return up_str;
}

