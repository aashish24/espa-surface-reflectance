/*****************************************************************************
FILE: output.c
  
PURPOSE: Contains functions for handling of the output data files for this
application.

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

LICENSE TYPE:  NASA Open Source Agreement Version 1.3

NOTES:
1. Pre-collection products have two QA bands.  The cloud QA contains some cloud
   related (as well as fill) information in an 8-bit bit-packed format.  The
   ipflag QA band contains aerosol information in an 8-bit bit-packed format.
2. Collection products have one QA band.  The cloud QA band will contain the
   cloud, fill, and aerosol QA information in a 16-bit bit-packed band.
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
    bool toa,                       /* I: set this structure up for the TOA
                                          bands vs. the SR bands */
    bool process_collection         /* I: should this scene be processed as a
                                          collection product, which affects
                                          the output of QA bands */
)
{
    Output_t *this = NULL;
    char FUNC_NAME[] = "open_output";   /* function name */
    char errmsg[STR_SIZE];       /* error message */
    char *upper_str = NULL;      /* upper case version of the SI short name */
    char scene_name[STR_SIZE];   /* scene name for the current scene */
    char production_date[MAX_DATE_LEN+1]; /* current date/time for production */
    time_t tp;                   /* time structure */
    struct tm *tm = NULL;        /* time structure for UTC time */
    int ib;    /* looping variable for bands */
    int refl_indx = -1;          /* band index in XML file for the reflectance
                                    band */
    Espa_band_meta_t *bmeta = NULL;  /* pointer to the band metadata array
                                        within the output structure */
    int nband;                   /* number of output bands to be created */

    /* Create the Output data structure */
    this = (Output_t *) malloc (sizeof (Output_t));
    if (this == NULL) 
    {
        sprintf (errmsg, "Error allocating Output data structure");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
  
    /* Find the representative band for metadata information.  Use band 1. */
    for (ib = 0; ib < in_meta->nbands; ib++)
    {
        if (!strcmp (in_meta->band[ib].name, "band1") &&
            !strncmp (in_meta->band[ib].product, "L1", 2))
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
    init_metadata_struct (&this->metadata);

    /* Copy the instrument type */
    this->inst = input->meta.inst;

    /* If processing collection products, there is one less QA band */
    nband = NBAND_TTL_OUT;
    if (process_collection)
        nband--;
    this->process_collection = process_collection;

    /* Allocate memory for the total bands */
    if (allocate_band_metadata (&this->metadata, nband) != SUCCESS)
    {
        sprintf (errmsg, "Allocating band metadata.");
        error_handler (true, FUNC_NAME, errmsg);
        return (NULL);
    }
    bmeta = this->metadata.band;

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
    this->open = false;
    this->nband = nband;
    this->nlines = input->size.nlines;
    this->nsamps = input->size.nsamps;
    for (ib = 0; ib < this->nband; ib++)
        this->fp_bin[ib] = NULL;
 
    for (ib = 0; ib < nband; ib++)
    {
        strncpy (bmeta[ib].short_name, in_meta->band[refl_indx].short_name, 3);
        bmeta[ib].short_name[3] = '\0';
        if (toa)
        {
            strcat (bmeta[ib].short_name, "TOA");
            if ((ib == SR_BAND10) || (ib == SR_BAND11))
                strcpy (bmeta[ib].product, "toa_bt");
            else
                strcpy (bmeta[ib].product, "toa_refl");
        }
        else
        {
            strcat (bmeta[ib].short_name, "SR");
            strcpy (bmeta[ib].product, "sr_refl");
        }

        bmeta[ib].nlines = this->nlines;
        bmeta[ib].nsamps = this->nsamps;
        bmeta[ib].pixel_size[0] = input->size.pixsize[0];
        bmeta[ib].pixel_size[1] = input->size.pixsize[1];
        strcpy (bmeta[ib].pixel_units, "meters");
        sprintf (bmeta[ib].app_version, "LaSRC_%s",
            SR_VERSION);
        strcpy (bmeta[ib].production_date, production_date);

        /* Handle the cloud/ipflag bands differently.  If this is only TOA then
           we don't need to process the cloud or ipflag mask.  If this is SR,
           then we don't need to process the cirrus or thermal bands. */
        if (toa && (ib == SR_CLOUD || ib == SR_IPFLAG))
            continue;
        else if (!toa &&
            ((ib == SR_BAND9) || (ib == SR_BAND10) || (ib == SR_BAND11)))
            continue;
        else if (ib == SR_CLOUD)
        {
            /* Common QA band fields */
            bmeta[ib].fill_value = CLOUD_FILL_VALUE;
            strcpy (bmeta[ib].name, "sr_cloud");
            strcpy (bmeta[ib].long_name, "surface reflectance cloud mask");
            strcpy (bmeta[ib].category, "qa");
            strcpy (bmeta[ib].data_units, "quality/feature classification");

            /* If processing pre-collection handle as a cloud QA. If processing
               as a collection product, handle as the overall QA. */
            if (!process_collection)
            {  /* Pre-collection: cloud QA as an 8-bit band */
                bmeta[ib].data_type = ESPA_UINT8;
    
                /* Set up cloud bitmap information */
                if (allocate_bitmap_metadata (&bmeta[ib], 8) != SUCCESS)
                {
                    sprintf (errmsg, "Allocating cloud bitmap.");
                    error_handler (true, FUNC_NAME, errmsg);
                    return (NULL);
                }
    
                /* Identify the bitmap values for the mask */
                strcpy (bmeta[ib].bitmap_description[0], "cirrus cloud");
                strcpy (bmeta[ib].bitmap_description[1], "cloud");
                strcpy (bmeta[ib].bitmap_description[2], "adjacent to cloud");
                strcpy (bmeta[ib].bitmap_description[3], "cloud shadow");
                strcpy (bmeta[ib].bitmap_description[4], "aerosol");
                strcpy (bmeta[ib].bitmap_description[5], "aerosol");
                strcpy (bmeta[ib].bitmap_description[6], "unused");
                strcpy (bmeta[ib].bitmap_description[7], "internal test");
            }
            else
            {  /* Collection: full QA as a 16-bit band */
                bmeta[ib].data_type = ESPA_UINT16;
    
                /* Set up cloud bitmap information */
                if (allocate_bitmap_metadata (&bmeta[ib], 11) != SUCCESS)
                {
                    sprintf (errmsg, "Allocating cloud bitmap.");
                    error_handler (true, FUNC_NAME, errmsg);
                    return (NULL);
                }
    
                /* Identify the bitmap values for the mask */
                strcpy (bmeta[ib].bitmap_description[0], "cirrus cloud");
                strcpy (bmeta[ib].bitmap_description[1], "cloud");
                strcpy (bmeta[ib].bitmap_description[2], "adjacent to cloud");
                strcpy (bmeta[ib].bitmap_description[3], "cloud shadow");
                strcpy (bmeta[ib].bitmap_description[4], "aerosol");
                strcpy (bmeta[ib].bitmap_description[5], "aerosol");
                strcpy (bmeta[ib].bitmap_description[6], "reserved for snow");
                strcpy (bmeta[ib].bitmap_description[7], "deep water");
                strcpy (bmeta[ib].bitmap_description[8], "water");
                strcpy (bmeta[ib].bitmap_description[9], "aerosol retrieval");
                strcpy (bmeta[ib].bitmap_description[10],
                    "aerosol interpolation");
            }
        }
        else if (ib == SR_IPFLAG)
        {  /* Not processed for Collection products */
            bmeta[ib].data_type = ESPA_UINT8;
            bmeta[ib].fill_value = IPFLAG_FILL;
            strcpy (bmeta[ib].name, "sr_ipflag");
            strcpy (bmeta[ib].long_name,
                "surface reflectance interpolation flag");
            strcpy (bmeta[ib].category, "qa");
            strcpy (bmeta[ib].data_units, "quality/feature classification");
            bmeta[ib].valid_range[0] = 0.0;
            bmeta[ib].valid_range[1] = 6.0;

            /* Set up ipflag class information */
            if (allocate_class_metadata (&bmeta[ib], 7) != SUCCESS)
            {
                sprintf (errmsg, "Allocating ipflag classes.");
                error_handler (true, FUNC_NAME, errmsg);
                return (NULL);
            }
          
            /* Identify the class values for the ipflag */
            bmeta[ib].class_values[0].class = 0;
            bmeta[ib].class_values[1].class = 1;
            bmeta[ib].class_values[2].class = 2;
            bmeta[ib].class_values[3].class = 3;
            bmeta[ib].class_values[4].class = 4;
            bmeta[ib].class_values[5].class = 5;
            bmeta[ib].class_values[6].class = 6;
            strcpy (bmeta[ib].class_values[0].description,
                "aerosol retrieval successful");
            strcpy (bmeta[ib].class_values[1].description,
                "aerosol retrieval failed, but aerosol was interpolated");
            strcpy (bmeta[ib].class_values[2].description,
                "NDVI test failed, but aerosol wasn't interpolated");
            strcpy (bmeta[ib].class_values[3].description,
                "residual test failed, but aerosol wasn't interpolated");
            strcpy (bmeta[ib].class_values[4].description, "not used");
            strcpy (bmeta[ib].class_values[5].description, "fill pixel");
            strcpy (bmeta[ib].class_values[6].description, "water pixel");
        }
        else
        {
            bmeta[ib].data_type = ESPA_INT16;
            bmeta[ib].fill_value = FILL_VALUE;
            strcpy (bmeta[ib].category, "image");
            strcpy (bmeta[ib].data_units, "reflectance");

            if (ib == SR_BAND10 || ib == SR_BAND11)  /* thermal bands */
            {
                bmeta[ib].scale_factor = SCALE_FACTOR_TH;
                bmeta[ib].valid_range[0] = (float) MIN_VALID_TH;
                bmeta[ib].valid_range[1] = (float) MAX_VALID_TH;
            }
            else
            {
                bmeta[ib].scale_factor = SCALE_FACTOR;
                bmeta[ib].valid_range[0] = (float) MIN_VALID;
                bmeta[ib].valid_range[1] = (float) MAX_VALID;
            }

            if (ib >= SR_BAND1 && ib <= SR_BAND7)
            {
                if (toa)
                {
                    sprintf (bmeta[ib].name, "toa_band%d", ib+1);
                    sprintf (bmeta[ib].long_name, "band %d top-of-atmosphere "
                        "reflectance", ib+1);
                }
                else
                {
                    sprintf (bmeta[ib].name, "sr_band%d", ib+1);
                    sprintf (bmeta[ib].long_name, "band %d surface reflectance",
                        ib+1);
                }
            }
            else if (ib == SR_BAND9)  /* cirrus band */
            {  /* band 9 is only atmospherically corrected */
                sprintf (bmeta[ib].name, "toa_band%d", ib+2);
                sprintf (bmeta[ib].long_name, "band %d top-of-atmosphere "
                    "reflectance", ib+2);
            }
            else if (ib == SR_BAND10 || ib == SR_BAND11)  /* thermal bands */
            {
                sprintf (bmeta[ib].name, "toa_band%d", ib+2);
                sprintf (bmeta[ib].long_name, "band %d at-satellite brightness "
                    "temperature", ib+2);
                sprintf (bmeta[ib].data_units, "temperature (kelvin)");
            }
        }

        /* Set up the filename with the scene name and band name and open the
           file for read/write access.  Don't open if this is OLI-only and
           these are the thermal bands. */
        if ((ib != SR_BAND10 && ib != SR_BAND11) || this->inst != INST_OLI)
        {
            sprintf (bmeta[ib].file_name, "%s_%s.img", scene_name,
                bmeta[ib].name);
            this->fp_bin[ib] = open_raw_binary (bmeta[ib].file_name, "w+");
            if (this->fp_bin[ib] == NULL)
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
    this->open = true;

    /* Successful completion */
    return this;
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
    Output_t *this,   /* I/O: Output data structure to close */
    bool toa          /* I: output structure is for TOA bands vs. SR bands */
)
{
    char FUNC_NAME[] = "close_output";   /* function name */
    char errmsg[STR_SIZE];    /* error message */
    int ib;                   /* looping variable */

    if (!this->open)
    {
        sprintf (errmsg, "File is not open, so it cannot be closed.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    /* Close raw binary products */
    for (ib = 0; ib < this->nband; ib++)
    {
        if (toa && (ib == SR_CLOUD || ib == SR_IPFLAG))
            continue;
        else if (!toa &&
            ((ib == SR_BAND9) || (ib == SR_BAND10) || (ib == SR_BAND11)))
            continue;
        else
        {
            /* No thermal bands are open for OLI-only scenes */
            if ((ib != SR_BAND10 && ib != SR_BAND11) || this->inst != INST_OLI)
                close_raw_binary (this->fp_bin[ib]);
        }
    }
    this->open = false;

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
    Output_t *this    /* I/O: Output data structure to free */
)
{
    char FUNC_NAME[] = "free_output";   /* function name */
    char errmsg[STR_SIZE];    /* error message */
    int b;                    /* looping variable for the bits */
  
    if (this->open) 
    {
        sprintf (errmsg, "File is still open, so cannot free memory.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
  
    if (this != NULL)
    {
        /* Free the bitmap data for the cloud band */
        if (this->metadata.band[SR_CLOUD].nbits > 0)
        {
            for (b = 0; b < this->metadata.band[SR_CLOUD].nbits; b++)
                free (this->metadata.band[SR_CLOUD].bitmap_description[b]);
            free (this->metadata.band[SR_CLOUD].bitmap_description);
        }

        /* Free the bitmap data for the ipflag band, if it exists */
        if (!this->process_collection)
            free (this->metadata.band[SR_IPFLAG].class_values);

        /* Free the band data */
        free (this->metadata.band);

        /* Free the data structure */
        free (this);
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
    Output_t *this,    /* I: Output data structure; buf contains the line to
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
    if (this == (Output_t *)NULL) 
    {
        sprintf (errmsg, "Invalid input structure");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (!this->open)
    {
        sprintf (errmsg, "File is not open.  Cannot write data.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (iband < 0 || iband >= this->nband)
    {
        sprintf (errmsg, "Invalid band number.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (iline < 0 || iline >= this->nlines)
    {
        sprintf (errmsg, "Invalid line number.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
    if (nlines < 0 || iline+nlines > this->nlines)
    {
        sprintf (errmsg, "Line plus number of lines to be written exceeds "
            "the predefined size of the image.");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }
  
    /* Write the data, but first seek to the correct line */
    loc = (long) iline * this->nsamps * nbytes;
    if (fseek (this->fp_bin[iband], loc, SEEK_SET))
    {
        sprintf (errmsg, "Seeking to the current line in the output file for "
            "band %d", iband);
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    if (write_raw_binary (this->fp_bin[iband], nlines, this->nsamps, nbytes,
        buf) != SUCCESS)
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

