/* -*- mode: c; c-file-style:"stroustrup"; -*- */

/*
 * Copyright (c) 2018 Mastercard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "pkcs11lib.h"
#include "wrappedkey_lexer.h"
#include "wrappedkey_parser.h"



/* private funcs prototypes */
static func_rc _output_wrapped_key_header(wrappedKeyCtx *wctx, FILE *fp);
static func_rc _output_wrapped_key_attributes(wrappedKeyCtx *wctx, FILE *fp);
static func_rc _output_wrapped_key_b64(wrappedKeyCtx *wctx, FILE *fp);

static func_rc _wrap_pkcs1_15(wrappedKeyCtx *wctx);
static func_rc _wrap_pkcs1_oaep(wrappedKeyCtx *wctx);
static func_rc _wrap_cbcpad(wrappedKeyCtx *wctx);


static char const *get_str_for_wrapping_algorithm(enum wrappingmethod w)
{
    char *rc;
    switch(w) {
    case w_pkcs1_15:
	rc = "PKCS#1 1.5";
	break;
	
    case w_pkcs1_oaep:
	rc = "PKCS#1 OAEP";
	break;
	
    case w_cbcpad:
	rc = "PKCS#11 CKM_xxx_CBC_PAD, with PKCS#7 padding";
	break;
	
    default:
	rc = "Unknown???";
    }

    return rc;
}

static void fprintf_key_type(FILE *fp, char *unused, CK_ATTRIBUTE_PTR attr)
{

    char *value;
    switch( *(CK_KEY_TYPE *)attr->pValue) {
	
    case CKK_GENERIC_SECRET:
	value = "CKK_GENERIC_SECRET";
	break;
	
    case CKK_DES:
	value = "CKK_DES";
	break;
	
    case CKK_DES2:
	value = "CKK_DES2";
	break;
	
    case CKK_DES3:
	value = "CKK_DES3";
	break;
	
    case CKK_AES:
	value = "CKK_AES";
	break;

    case CKK_MD5_HMAC:
        value = "CKK_MD5_HMAC";
	break;
	
    case CKK_SHA_1_HMAC:
	value = "CKK_SHA_1_HMAC";
	break;
	
    case CKK_RIPEMD128_HMAC:
	value = "CKK_RIPEMD128_HMAC";
	break;
	
    case CKK_RIPEMD160_HMAC:
	value = "CKK_RIPEMD160_HMAC";
	break;
	
    case CKK_SHA256_HMAC:
	value = "CKK_SHA256_HMAC";
	break;
	
    case CKK_SHA384_HMAC:
	value = "CKK_SHA384_HMAC";
	break;
	
    case CKK_SHA512_HMAC:
	value = "CKK_SHA512_HMAC";
	break;
	
    case CKK_SHA224_HMAC:
	value = "CKK_SHA224_HMAC";
	break;

    case CKK_RSA:
	value = "CKK_RSA";
	break;

    case CKK_DSA:
	value = "CKK_RSA";
	break;

    case CKK_DH:
	value = "CKK_DH";
	break;

    case CKK_EC:
	value = "CKK_EC";
	break;
	
    default:
	value = "unsupported";
    }

    fprintf(fp, "CKA_KEY_TYPE: %s\n", value);

}


static void fprintf_object_class(FILE *fp, char *unused, CK_ATTRIBUTE_PTR attr)
{

    char *value;
	
    
    switch( *(CK_OBJECT_CLASS *)attr->pValue ) {
    case CKO_DATA:
	value = "CKO_DATA";
	break;
	
    case CKO_CERTIFICATE:
	value = "CKO_CERTIFICATE";
	break;
	
    case CKO_PUBLIC_KEY:
	value = "CKO_PUBLIC_KEY";
	break;
	
    case CKO_PRIVATE_KEY:
	value = "CKO_PRIVATE_KEY";
	break;
	
    case CKO_SECRET_KEY:
	value = "CKO_SECRET_KEY";
	break;
	
    case CKO_HW_FEATURE:
	value = "CKO_HW_FEATURE";
	break;
	
    case CKO_DOMAIN_PARAMETERS:
	value = "CKO_DOMAIN_PARAMETERS";
	break;
	
    case CKO_MECHANISM:
	value = "CKO_MECHANISM";
	break;
	
    case CKO_OTP_KEY:
	value = "CKO_OTP_KEY";
	break;
	
    default:
	value = (*(CK_OBJECT_CLASS *)attr->pValue) & CKO_VENDOR_DEFINED ? "CKO_VENDOR_DEFINED" : "??unknown object type??" ;
	break;
    }
    
    fprintf(fp, "CKA_CLASS: %s\n", value);
}


static void fprintf_boolean_attr(FILE *fp, char *name, CK_ATTRIBUTE_PTR attr)
{
    fprintf (fp, "%s: %s\n", name, *((CK_BBOOL *)(attr->pValue))== CK_TRUE ? "true" : "false");
}

static void fprintf_hex_attr(FILE *fp, char *name, CK_ATTRIBUTE_PTR attr)
{
    int i;

    fprintf(fp, "%s: 0x", name);
    for(i=0; i<attr->ulValueLen; i++) {
	fprintf(fp, "%02x", ((unsigned char* )(attr->pValue))[i] );
    }

    fprintf(fp, "\n");
}

/* _fprintf_str_attr not meant to be used directly, as there is no check about printability */
/* use fprintf_str_attr or fprintf_date_attr instead */
static void _fprintf_str_attr(FILE *fp, char *name, CK_ATTRIBUTE_PTR attr)
{
    fprintf(fp, "%s: \"%.*s\"\n", name, (int)(attr->ulValueLen), (unsigned char* )(attr->pValue));
}


/* check if we can print it as a string (i.e. no special character) */
/* otherwise, print as hex. */

static void fprintf_str_attr(FILE *fp, char *name, CK_ATTRIBUTE_PTR attr)
{
    int seems_printable = 0;
    int i;
    
    /* simple check: verify all can be printed */
    for(i=0; i<attr->ulValueLen; i++) {
	if(!isprint(((unsigned char *)(attr->pValue))[i])) {
	    goto not_printable; /* exit loop prematurely */
	}	    
    }
    seems_printable = 1;

not_printable:
	/* do nothing, seems_printable worths 0 */    

    seems_printable ? _fprintf_str_attr(fp,name,attr) : fprintf_hex_attr(fp,name,attr);

}

/* date is a special case. If it looks like a date, print it in plain characters */
/* otherwise take no risk and print as hex value */

static void fprintf_date_attr(FILE *fp, char *name, CK_ATTRIBUTE_PTR attr)
{
    int looks_like_a_date = 0;

    if(attr->ulValueLen==8) {
	int i;
	/* simple check: verify we have digits everywhere */
	/* a more sophisticated one would check if it looks like a REAL date... */
	for(i=0; i<8; i++) {
	    if(!isdigit(((unsigned char *)(attr->pValue))[i])) {
		goto not_a_digit; /* exit loop prematurely */
	    }	    
	}
	looks_like_a_date = 1;
    }

not_a_digit:
	/* do nothing, looks_like_a_date worths 0 */    

    looks_like_a_date ? _fprintf_str_attr(fp,name,attr) : fprintf_hex_attr(fp,name,attr);
}


/*------------------------------------------------------------------------*/


static char * sprintf_hex_buffer(CK_BYTE_PTR buffer, CK_ULONG len)
{

    char *allocated = malloc(len*2+3);

    if(allocated==NULL) {
	fprintf(stderr, "***Error: memory allocation\n");
    } else {
	
	int i;

	allocated[0]='0';
	allocated[1]='x';

	for(i=0; i<len; i++) {
	    snprintf(&allocated[2+i*2], 3, "%02x", buffer[i] );
	}
    }

    return allocated;
}

/* _sprintf_str_buffer not meant to be used directly, as there is no check about printability */
/* use sprintf_str_buffer_safe instead */
static char * _sprintf_str_buffer(CK_BYTE_PTR buffer, CK_ULONG len)
{
    char *allocated = malloc(len+3);

    if(allocated==NULL) {
	fprintf(stderr, "***Error: memory allocation\n");
    } else {
	snprintf(allocated,len+3, "\"%.*s\"", (int)len, buffer);
    }
    return allocated;
}


/* check if we can print the buffer as a string (i.e. no special character) */
/* otherwise, print as hex. */

static char * sprintf_str_buffer_safe(CK_BYTE_PTR buffer, CK_ULONG len)
{
    int seems_printable = 0;
    int i;

    /* simple check: verify all can be printed */
    for(i=0; i<len; i++) {
	if(!isprint(buffer[i])) {
	    goto not_printable; /* exit loop prematurely */
	}	    
    }
    seems_printable = 1;

not_printable:
	/* do nothing, seems_printable worths 0 */    

    return seems_printable ? _sprintf_str_buffer(buffer,len) : sprintf_hex_buffer(buffer,len);

}

static void free_sprintf_str_buffer_safe_buf(char *ptr)
{
    if(ptr) { free(ptr); }
}

/*------------------------------------------------------------------------*/


static char * const _mgfstring(CK_RSA_PKCS_MGF_TYPE mgf)
{
    char *retval = NULL;
    
    switch(mgf) {
    case CKG_MGF1_SHA1:
	retval =  "CKG_MGF1_SHA1";
	break;
	
    case CKG_MGF1_SHA256:
	retval = "CKG_MGF1_SHA256";
	break;
	
    case CKG_MGF1_SHA384:
	retval = "CKG_MGF1_SHA384";
	break;
	
    case CKG_MGF1_SHA512:
	retval = "CKG_MGF1_SHA512";
	break;
	
    case CKG_MGF1_SHA224:
	retval = "CKG_MGF1_SHA224";
	break;
	
    }	

    return retval;
}

static char * const _hashstring(CK_MECHANISM_TYPE hash)
{
    char *retval = NULL;

    switch(hash) {
    case CKM_MD2:
	retval = "CKM_MD2";
	break;
	
    case CKM_MD5:
	retval = "CKM_MD5";
	break;
	
    case CKM_SHA_1:
	retval = "CKM_SHA_1";
	break;
	
    case CKM_RIPEMD128:
	retval = "CKM_RIPEMD128";
	break;
	
    case CKM_RIPEMD160:
	retval = "CKM_RIPEMD160";
	break;
	
    case CKM_SHA256:
	retval = "CKM_SHA256";
	break;
	
    case CKM_SHA224:
	retval = "CKM_SHA224";
	break;
	
    case CKM_SHA384:
	retval = "CKM_SHA384";
	break;
	
    case CKM_SHA512:
	retval = "CKM_SHA512";
	break;
    }

    return retval;

}

static func_rc _output_wrapped_key_header(wrappedKeyCtx *wctx, FILE *fp)
{

    time_t now = time(NULL);
    char hostname[255];
    char *label;
    
    gethostname(hostname, 255);
    hostname[254]=0;		/* just to be sure... */

    char *wrappingalgstr;

    switch(wctx->wrapping_meth) {
    case w_pkcs1_15:
	wrappingalgstr = "pkcs1";
	break;

    case w_pkcs1_oaep:
	wrappingalgstr = "oaep";
	break;

    case w_cbcpad:
	wrappingalgstr = "cbcpad";
	break;

    default:
	fprintf(stderr, "Error: unsupported wrapping algorithm.\n");
	return rc_error_unknown_wrapping_alg;
    }
	
    fprintf(fp, \
	    "########################################################################\n"
	    "#\n"
	    "# key <%s> wrapped by key <%s>\n"
	    "# wrapped on host <%s>\n"
	    "# operation date and time (UTC): %s"
	    "# wrapping algorithm: %s\n"
	    "#\n"
	    "# use p11unwrap from pkcs11-tools to unwrap key on dest. PKCS#11 token\n"
	    "#\n"
	    "# grammar for this file:\n"
	    "# ----------------------\n"
	    "#\n"	    
	    "# - lines starting with '#' are ignored\n"
	    "#\n"
	    "# - [ATTRIBUTE] : [VALUE]\n"
	    "#   where [ATTRIBUTE] is any of the following:\n"
	    "#     Content-Type ( value is application/pkcs11-tools)\n"
	    "#     Wrapping-Algorithm: (value is pkcs1/1.0)\n"
	    "#     CKA_LABEL\n"
	    "#     CKA_ID\n"
	    "#     CKA_CLASS\n"
	    "#     CKA_TOKEN\n"
	    "#     CKA_KEY_TYPE\n"
	    "#     CKA_ENCRYPT\n"
	    "#     CKA_DECRYPT\n"
	    "#     CKA_WRAP\n"
	    "#     CKA_UNWRAP\n"
	    "#     CKA_SIGN\n"
	    "#     CKA_VERIFY\n"
	    "#     CKA_DERIVE\n"
	    "#     CKA_PRIVATE\n"
	    "#     CKA_SENSITIVE\n"
	    "#     CKA_EXTRACTABLE\n"
	    "#     CKA_MODIFIABLE\n"
	    "#     CKA_START_DATE\n"
	    "#     CKA_END_DATE\n"
	    "#     CKA_CHECK_VALUE\n"
	    "#   where, depending on the attribute, [VALUE] can be one of the following:\n"
            "#     \"Hello world\" (printable string)\n"
	    "#      0x1A2B3C4D (hex bytes)\n"
	    "#      20150630   (date)\n"
	    "#      true/false/CK_TRUE/CK_FALSE/yes/no (boolean)\n"
	    "#\n"
	    "# - wrapped key is contained between -----BEGIN WRAPPED KEY----- \n"
            "#   and -----END WRAPPED KEY----- marks and is Base64 encoded\n"
	    "#\n"
	    "########################################################################\n"
	    "Content-Type: application/pkcs11-tools\n"
	    "Wrapping-Key: \"%s\"\n",
	    wctx->wrappedkeylabel,
	    wctx->wrappingkeylabel,
	    hostname,
	    asctime(gmtime(&now)),
	    get_str_for_wrapping_algorithm(wctx->wrapping_meth),
	    wctx->wrappingkeylabel );


    switch(wctx->wrapping_meth) {

    case w_pkcs1_15:
	fprintf(fp, "Wrapping-Algorithm: %s/1.0\n", "pkcs1");
	break;
	
    /* we have one additional parameter for oaep: the label (in PKCS#1), referred as source in PKCS#11 */
    case w_pkcs1_oaep: {

	char *labelstring=sprintf_str_buffer_safe( wctx->oaep_params->pSourceData, wctx->oaep_params->ulSourceDataLen);
	
	fprintf(fp, "Wrapping-Algorithm: %s/1.0(hash=%s,mgf=%s,label=%s)\n",
		"oaep",
		_hashstring(wctx->oaep_params->hashAlg),
		_mgfstring(wctx->oaep_params->mgf),
		wctx->oaep_params->pSourceData==NULL ? "\"\"" : labelstring );

	free_sprintf_str_buffer_safe_buf(labelstring);
    }	
	break;

    case w_cbcpad: {
	char *labelstring=sprintf_str_buffer_safe( wctx->iv, wctx->iv_len );
	
	fprintf(fp, "Wrapping-Algorithm: %s/1.0(iv=%s)\n",
		"cbcpad",
		labelstring );

	free_sprintf_str_buffer_safe_buf(labelstring);

    }
	break;
	
    default:
	break;
    }
    
    return rc_ok;
}

static func_rc _output_wrapped_key_b64(wrappedKeyCtx *wctx, FILE *fp)
{

    func_rc rc = rc_ok;
    BIO *bio_stdout = NULL, *bio_b64 = NULL;

    bio_b64 = BIO_new( BIO_f_base64() );
    if(bio_b64==NULL) {
	P_ERR();
	rc = rc_error_openssl_api;
	goto err;
    }
    
    bio_stdout = BIO_new( BIO_s_file() );
    if(bio_stdout==NULL) {
	P_ERR();
	rc = rc_error_openssl_api;
	goto err;
    }

    BIO_set_fp(bio_stdout, fp, BIO_NOCLOSE);

    BIO_puts(bio_stdout, "-----BEGIN WRAPPED KEY-----\n");
    BIO_flush(bio_stdout);
    BIO_push(bio_b64, bio_stdout);
    BIO_write(bio_b64, wctx->wrapped_key_buffer, wctx->wrapped_key_len);
    BIO_flush(bio_b64);
    BIO_puts(bio_stdout, "-----END WRAPPED KEY-----\n");
    BIO_flush(bio_stdout);

 err:    
    if(bio_stdout) BIO_free(bio_stdout);
    if(bio_b64) BIO_free(bio_b64);

    return rc;
}


static func_rc _output_wrapped_key_attributes(wrappedKeyCtx *wctx, FILE *fp)
{
    func_rc rc = rc_ok;

    pkcs11AttrList *wrappedkey_attrs = NULL;
    CK_ATTRIBUTE_PTR o_attr = NULL;
    size_t alist_len=0;

    typedef struct {
	CK_ATTRIBUTE_TYPE attr_type;
	void (*func_ptr) (FILE *, char *, CK_ATTRIBUTE_PTR );
	char *name;
    } attr_printer ;
    
    attr_printer seckalist[] = {
	{ CKA_LABEL, fprintf_str_attr, "CKA_LABEL" },
	{ CKA_ID, fprintf_str_attr, "CKA_ID" },
	{ CKA_CLASS, fprintf_object_class, "CKA_CLASS" },
	{ CKA_TOKEN, fprintf_boolean_attr, "CKA_TOKEN" },
	{ CKA_KEY_TYPE, fprintf_key_type, "CKA_KEY_TYPE" },
	{ CKA_ENCRYPT, fprintf_boolean_attr, "CKA_ENCRYPT" },
	{ CKA_DECRYPT, fprintf_boolean_attr, "CKA_DECRYPT" },
	{ CKA_WRAP, fprintf_boolean_attr, "CKA_WRAP" },
	{ CKA_UNWRAP, fprintf_boolean_attr, "CKA_UNWRAP" },
	{ CKA_SIGN, fprintf_boolean_attr, "CKA_SIGN" },
	{ CKA_VERIFY, fprintf_boolean_attr, "CKA_VERIFY" },
	{ CKA_DERIVE, fprintf_boolean_attr, "CKA_DERIVE" },
	{ CKA_PRIVATE, fprintf_boolean_attr, "CKA_PRIVATE" },
	{ CKA_SENSITIVE, fprintf_boolean_attr, "CKA_SENSITIVE" },
	{ CKA_EXTRACTABLE, fprintf_boolean_attr, "CKA_EXTRACTABLE" },
	{ CKA_MODIFIABLE, fprintf_boolean_attr, "CKA_MODIFIABLE" },
	{ CKA_START_DATE, fprintf_date_attr, "CKA_START_DATE" },
	{ CKA_END_DATE, fprintf_date_attr, "CKA_END_DATE" },	    
	{ CKA_CHECK_VALUE, fprintf_hex_attr, "CKA_CHECK_VALUE" },	    
    };

    attr_printer prvkalist[] = {
	{ CKA_LABEL, fprintf_str_attr, "CKA_LABEL" },
	{ CKA_ID, fprintf_str_attr, "CKA_ID" },
	{ CKA_CLASS, fprintf_object_class, "CKA_CLASS" },
	{ CKA_TOKEN, fprintf_boolean_attr, "CKA_TOKEN" },
	{ CKA_KEY_TYPE, fprintf_key_type, "CKA_KEY_TYPE" },
	{ CKA_EC_PARAMS, fprintf_hex_attr, "CKA_EC_PARAMS" },
	{ CKA_SUBJECT, fprintf_hex_attr, "CKA_SUBJECT" },
	{ CKA_DECRYPT, fprintf_boolean_attr, "CKA_DECRYPT" },
	{ CKA_UNWRAP, fprintf_boolean_attr, "CKA_UNWRAP" },
	{ CKA_SIGN, fprintf_boolean_attr, "CKA_SIGN" },
	{ CKA_SIGN_RECOVER, fprintf_boolean_attr, "CKA_SIGN_RECOVER" },
	{ CKA_DERIVE, fprintf_boolean_attr, "CKA_DERIVE" },
	{ CKA_PRIVATE, fprintf_boolean_attr, "CKA_PRIVATE" },
	{ CKA_SENSITIVE, fprintf_boolean_attr, "CKA_SENSITIVE" },
	{ CKA_EXTRACTABLE, fprintf_boolean_attr, "CKA_EXTRACTABLE" },
	{ CKA_MODIFIABLE, fprintf_boolean_attr, "CKA_MODIFIABLE" },
	{ CKA_START_DATE, fprintf_date_attr, "CKA_START_DATE" },
	{ CKA_END_DATE, fprintf_date_attr, "CKA_END_DATE" },	    
    };

    attr_printer *alist=NULL;
    
    switch(wctx->wrappedkeyobjclass) {
    case CKO_SECRET_KEY:
	alist = seckalist;
	alist_len = sizeof(seckalist)/sizeof(attr_printer);
	wrappedkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					       _ATTR(CKA_LABEL),
					       _ATTR(CKA_ID),
					       _ATTR(CKA_CLASS),
					       _ATTR(CKA_TOKEN),
					       _ATTR(CKA_KEY_TYPE),
					       _ATTR(CKA_ENCRYPT),
					       _ATTR(CKA_DECRYPT),
					       _ATTR(CKA_WRAP),
					       _ATTR(CKA_UNWRAP),
					       _ATTR(CKA_SIGN),
					       _ATTR(CKA_VERIFY),
					       _ATTR(CKA_DERIVE),
					       _ATTR(CKA_PRIVATE),
					       _ATTR(CKA_SENSITIVE),
					       _ATTR(CKA_EXTRACTABLE),
					       _ATTR(CKA_MODIFIABLE),
					       _ATTR(CKA_START_DATE),
					       _ATTR(CKA_END_DATE),
					       _ATTR(CKA_CHECK_VALUE),
					       _ATTR_END );
	break;

    case CKO_PRIVATE_KEY:
	alist = prvkalist;
	alist_len = sizeof(prvkalist)/sizeof(attr_printer);	
	wrappedkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					       _ATTR(CKA_LABEL),
					       _ATTR(CKA_ID),
					       _ATTR(CKA_CLASS),
					       _ATTR(CKA_TOKEN),
					       _ATTR(CKA_KEY_TYPE),
					       _ATTR(CKA_EC_PARAMS),
					       _ATTR(CKA_SUBJECT),
					       _ATTR(CKA_DECRYPT),
					       _ATTR(CKA_UNWRAP),
					       _ATTR(CKA_SIGN),
					       _ATTR(CKA_SIGN_RECOVER),
					       _ATTR(CKA_DERIVE),
					       _ATTR(CKA_PRIVATE),
					       _ATTR(CKA_SENSITIVE),
					       _ATTR(CKA_EXTRACTABLE),
					       _ATTR(CKA_MODIFIABLE),
					       _ATTR(CKA_START_DATE),
					       _ATTR(CKA_END_DATE),
					       _ATTR_END );
	break;

    default:
	fprintf(stderr,"***Error: Oops... invalid object type, bailing out\n");
	rc = rc_error_oops;
	goto error;
    }
	
    
    if( pkcs11_read_attr_from_handle (wrappedkey_attrs, wctx->wrappedkeyhandle) == CK_FALSE) {
	fprintf(stderr,"Error: could not read attributes from key with label '%s'\n", wctx->wrappedkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 

    {

	int i;

	for(i=0;i<alist_len;i++) {
	    o_attr = pkcs11_get_attr_in_attrlist(wrappedkey_attrs, alist[i].attr_type);
	
	    if(o_attr == NULL) {
		fprintf(fp, "# %s attribute not found\n", alist[i].name);
	    } else if (o_attr->ulValueLen == 0) {
		fprintf(fp, "# %s attribute is empty\n", alist[i].name);
	    } else {
		alist[i].func_ptr(fp, alist[i].name, o_attr);
	    }
	}
    }


error:

    pkcs11_delete_attrlist(wrappedkey_attrs);

    return rc;
    
}

static func_rc _wrap_pkcs1_15(wrappedKeyCtx *wctx)
{
    func_rc rc = rc_ok;
    
    CK_OBJECT_HANDLE hWrappingKey=NULL_PTR;
    CK_OBJECT_HANDLE hWrappedKey=NULL_PTR;
    pkcs11AttrList *wrappedkey_attrs = NULL, *wrappingkey_attrs = NULL;
    CK_ATTRIBUTE_PTR o_wrappingkey_bytes, o_wrappedkey_bytes, o_modulus;
    BIGNUM *bn_wrappingkey_bytes = NULL;
    BIGNUM *bn_wrappedkey_bytes = NULL;
    int bytelen;

    /* retrieve keys  */
    
    if (!pkcs11_findpublickey(wctx->p11Context, wctx->wrappingkeylabel, &hWrappingKey)) {
	fprintf(stderr,"Error: could not find a public key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    if(!pkcs11_findsecretkey(wctx->p11Context, wctx->wrappedkeylabel, &hWrappedKey)) {
	fprintf(stderr,"Error: secret key with label '%s' does not exists\n", wctx->wrappedkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    /* retrieve length of wrapping key */    
    wrappingkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					    _ATTR(CKA_MODULUS),
					    _ATTR_END );
    
    if( pkcs11_read_attr_from_handle (wrappingkey_attrs, hWrappingKey) == CK_FALSE) {
	fprintf(stderr,"Error: could not read CKA_MODULUS_BITS attribute from public key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 
    
    o_modulus = pkcs11_get_attr_in_attrlist(wrappingkey_attrs, CKA_MODULUS);

    /* overwrite existing value */
    if ( (bn_wrappingkey_bytes = BN_bin2bn(o_modulus->pValue, o_modulus->ulValueLen, NULL)) == NULL ) {
	P_ERR();
	goto error;
    }

    /* extract number of bytes */
    bytelen = BN_num_bytes(bn_wrappingkey_bytes);

    /* and adjust value */
    BN_set_word(bn_wrappingkey_bytes, (unsigned long)bytelen);
    
    /* retrieve length of wrapped key */
    wrappedkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					    _ATTR(CKA_VALUE_LEN), /* caution: value in bytes */
					    _ATTR_END );
    
    if( pkcs11_read_attr_from_handle (wrappedkey_attrs, hWrappedKey) == CK_FALSE) {
	fprintf(stderr,"Error: could not read CKA_VALUE_LEN attribute from secret key with label '%s'\n", wctx->wrappedkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 
    
    o_wrappedkey_bytes = pkcs11_get_attr_in_attrlist(wrappedkey_attrs, CKA_VALUE_LEN);

    /* BN_bin2bn works only with big endian, so we must alter data */
    /* if architecture is LE */

    *((CK_ULONG *)o_wrappedkey_bytes->pValue) = pkcs11_ll_bigendian_ul( *((CK_ULONG *)(o_wrappedkey_bytes->pValue))); /* transform if required */

    if ( (bn_wrappedkey_bytes = BN_bin2bn( o_wrappedkey_bytes->pValue, o_wrappedkey_bytes->ulValueLen, NULL)  ) == NULL ) {
	P_ERR();
	goto error;
    }
    
    /* now check that len(wrapped_key) < len(wrapping_key) - 11 */
    /* !! lengths being expressed in bytes */
    
    /* then add 11 to this value */

    if(! BN_add_word( bn_wrappedkey_bytes, 11L) ) {
	P_ERR();
	goto error;
    }

    /* if bn_wrapped_key  + 11 > bn_wrapping_key, then the wrapping key is too short.  */

    if( BN_cmp( bn_wrappedkey_bytes, bn_wrappingkey_bytes) > 0 ) {
	fprintf(stderr, "Error: wrapping key '%s' is too short to wrap key '%s'\n", wctx->wrappingkeylabel, wctx->wrappedkeylabel);
	rc = rc_error_wrapping_key_too_short;
	goto error;
    }


    /* we are good, let's allocate the memory and wrap */
    /* trick: we use now the CKA_MODULUS attribute to size the target buffer */

    wctx->wrapped_key_buffer = calloc ( o_modulus->ulValueLen, sizeof(unsigned char) );

    if(wctx->wrapped_key_buffer == NULL) {
	fprintf(stderr,"Error: memory\n");
	rc = rc_error_memory;
	goto error;
    }
    
    wctx->wrapped_key_len = o_modulus->ulValueLen;

    /* now wrap */

    {
	CK_RV rv;
	CK_MECHANISM mechanism = { CKM_RSA_PKCS, NULL_PTR, 0 };/* PKCS #1 1.5 wrap */
	
	rv = wctx->p11Context->FunctionList.C_WrapKey ( wctx->p11Context->Session,
							&mechanism,
							hWrappingKey,
							hWrappedKey,
							wctx->wrapped_key_buffer,
							&(wctx->wrapped_key_len) );
						 
	if(rv!=CKR_OK) {
	    pkcs11_error(rv, "C_WrapKey");
	    rc = rc_error_pkcs11_api;
	    goto error;
	}
	wctx->wrappedkeyhandle = hWrappedKey; /* keep a copy, for the output */
	wctx->wrappedkeyobjclass = CKO_SECRET_KEY; /* same story */
    }
   
error:
    if(bn_wrappingkey_bytes != NULL) { BN_free(bn_wrappingkey_bytes); bn_wrappingkey_bytes=NULL; }
    if(bn_wrappedkey_bytes != NULL) { BN_free(bn_wrappedkey_bytes); bn_wrappedkey_bytes=NULL; }
    pkcs11_delete_attrlist(wrappingkey_attrs);
    pkcs11_delete_attrlist(wrappedkey_attrs);

    return rc;
}



static func_rc _wrap_cbcpad(wrappedKeyCtx *wctx)
{
    func_rc rc = rc_ok;
    
    CK_OBJECT_HANDLE hWrappingKey=NULL_PTR;
    CK_OBJECT_HANDLE hWrappedKey=NULL_PTR;
    pkcs11AttrList *wrappedkey_attrs = NULL, *wrappingkey_attrs = NULL;
    CK_ATTRIBUTE_PTR o_keytype;
    CK_OBJECT_CLASS wrappedkeyobjclass;
    int bytelen;
    int blocklength;

    /* retrieve keys  */

    /* wrapping key is a secret key */
    if (!pkcs11_findsecretkey(wctx->p11Context, wctx->wrappingkeylabel, &hWrappingKey)) {
	fprintf(stderr,"***Error: could not find a secret key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    if(!pkcs11_findprivateorsecretkey(wctx->p11Context, wctx->wrappedkeylabel, &hWrappedKey, &wrappedkeyobjclass)) {
	fprintf(stderr,"***Error: key with label '%s' does not exists\n", wctx->wrappedkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    /* determining block size of the block cipher. */
    /* retrieve length of wrapping key */    
    wrappingkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					    _ATTR(CKA_KEY_TYPE),
					    _ATTR_END );


    if( pkcs11_read_attr_from_handle (wrappingkey_attrs, hWrappingKey) == CK_FALSE) {
	fprintf(stderr,"Error: could not read CKA_KEY_TYPE attribute from secret key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 

    o_keytype = pkcs11_get_attr_in_attrlist(wrappingkey_attrs, CKA_KEY_TYPE);

    switch(*(CK_KEY_TYPE *)(o_keytype->pValue)) {
    case CKK_AES:
	blocklength=16;
	break;

    case CKK_DES:
    case CKK_DES2:
    case CKK_DES3:
	blocklength=8;
	break;

    default:
	fprintf(stderr,"***Error: unsupported key type for wrapping key\n");
	rc = rc_error_unsupported;
	goto error;
    }

    /* check length of iv */

    if(wctx->iv_len==0) {
        /* special case: no IV was given - We do one of our own */
	wctx->iv=malloc(blocklength);
	if(wctx->iv==NULL) {
	    fprintf(stderr,"***Error: memory allocation\n");
	    rc = rc_error_memory;
	    goto error;
	}
	wctx->iv_len = blocklength;

	/* randomize it */
	pkcs11_getrandombytes(wctx->p11Context, wctx->iv,blocklength);
	
    } else {
	if(wctx->iv_len != blocklength) {
	    fprintf(stderr, "***Error: IV vector length(%d) mismatch - %d bytes are required\n", (int)(wctx->iv_len), (int)blocklength);
	    rc = rc_error_invalid_parameter_for_method;
	    goto error;
	}
    }
    
/* now wrap */

    {
	CK_RV rv;
	CK_MECHANISM mechanism = { 0L, wctx->iv, wctx->iv_len };
	CK_ULONG wrappedkeybuffersize;

	switch(*(CK_KEY_TYPE *)(o_keytype->pValue)) {
	case CKK_AES:
	    mechanism.mechanism = CKM_AES_CBC_PAD;
	    break;

	case CKK_DES:
	    mechanism.mechanism = CKM_DES_CBC_PAD;
	    break;

	case CKK_DES2:		/* DES2 and DES3 both use the same mechanism */
	case CKK_DES3:
	    mechanism.mechanism = CKM_DES3_CBC_PAD;
	    break;

	default:
	    fprintf(stderr,"***Error: unsupported key type for wrapping key\n");
	    rc = rc_error_unsupported;
	    goto error;
	}

	/* first call to know what will be the size output buffer */
	rv = wctx->p11Context->FunctionList.C_WrapKey ( wctx->p11Context->Session,
							&mechanism,
							hWrappingKey,
							hWrappedKey,
							NULL,
							&wrappedkeybuffersize );
	
	if(rv!=CKR_OK) {
	    pkcs11_error(rv, "C_WrapKey");
	    rc = rc_error_pkcs11_api;
	    goto error;
	}

	wctx->wrapped_key_buffer = malloc( wrappedkeybuffersize );
	if(wctx->wrapped_key_buffer==NULL) {
	    fprintf(stderr,"***Error: memory allocation\n");
	    rc = rc_error_memory;
	    goto error;
	}
	wctx->wrapped_key_len = wrappedkeybuffersize;

	/* now we can do the real call, with the real buffer */
	rv = wctx->p11Context->FunctionList.C_WrapKey ( wctx->p11Context->Session,
							&mechanism,
							hWrappingKey,
							hWrappedKey,
							wctx->wrapped_key_buffer,
							&(wctx->wrapped_key_len) );


	if(rv!=CKR_OK) {
	    pkcs11_error(rv, "C_WrapKey");
	    rc = rc_error_pkcs11_api;
	    goto error;
	}

	wctx->wrappedkeyobjclass = wrappedkeyobjclass;
	wctx->wrappedkeyhandle = hWrappedKey;
    }
   
error:
    pkcs11_delete_attrlist(wrappingkey_attrs);
    pkcs11_delete_attrlist(wrappedkey_attrs);

    return rc;
}



/*------------------------------------------------------------------------*/
static func_rc _wrap_pkcs1_oaep(wrappedKeyCtx *wctx)
{
    func_rc rc = rc_ok;
    
    CK_OBJECT_HANDLE hWrappingKey=NULL_PTR;
    CK_OBJECT_HANDLE hWrappedKey=NULL_PTR;
    pkcs11AttrList *wrappedkey_attrs = NULL, *wrappingkey_attrs = NULL;
    CK_ATTRIBUTE_PTR o_wrappingkey_bytes, o_wrappedkey_bytes, o_modulus, o_keytype;
    BIGNUM *bn_wrappingkey_bytes = NULL;
    BIGNUM *bn_wrappedkey_bytes = NULL;
    int bytelen;
    int sizeoverhead;
    unsigned long keysizeinbytes;

    /* retrieve keys  */
    
    if (!pkcs11_findpublickey(wctx->p11Context, wctx->wrappingkeylabel, &hWrappingKey)) {
	fprintf(stderr,"Error: could not find a public key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    if(!pkcs11_findsecretkey(wctx->p11Context, wctx->wrappedkeylabel, &hWrappedKey)) {
	fprintf(stderr,"Error: secret key with label '%s' does not exists\n", wctx->wrappedkeylabel);
	rc = rc_error_object_not_found;
	goto error;
    }

    /* retrieve length of wrapping key */    
    wrappingkey_attrs = pkcs11_new_attrlist(wctx->p11Context, 
					    _ATTR(CKA_MODULUS),
					    _ATTR_END );
    
    if( pkcs11_read_attr_from_handle (wrappingkey_attrs, hWrappingKey) == CK_FALSE) {
	fprintf(stderr,"Error: could not read CKA_MODULUS_BITS attribute from public key with label '%s'\n", wctx->wrappingkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 
    
    o_modulus = pkcs11_get_attr_in_attrlist(wrappingkey_attrs, CKA_MODULUS);

    /* overwrite existing value */
    if ( (bn_wrappingkey_bytes = BN_bin2bn(o_modulus->pValue, o_modulus->ulValueLen, NULL)) == NULL ) {
	P_ERR();
	goto error;
    }

    /* extract number of bytes */
    bytelen = BN_num_bytes(bn_wrappingkey_bytes);

    /* and adjust value */
    BN_set_word(bn_wrappingkey_bytes, (unsigned long)bytelen);
    
    /* retrieve length of wrapped key */
    wrappedkey_attrs = pkcs11_new_attrlist(wctx->p11Context,
					   _ATTR(CKA_KEY_TYPE), /* needed as CKA_VALUE_LEN might not always be present */
					   _ATTR(CKA_VALUE_LEN), /* caution: value in bytes */
					   _ATTR_END );
    
    if( pkcs11_read_attr_from_handle (wrappedkey_attrs, hWrappedKey) == CK_FALSE) {
	fprintf(stderr,"Error: could not read attributes from secret key with label '%s'\n", wctx->wrappedkeylabel);
	rc = rc_error_pkcs11_api;
	goto error;
    } 
    
    o_wrappedkey_bytes = pkcs11_get_attr_in_attrlist(wrappedkey_attrs, CKA_VALUE_LEN);
    /* pkcs11_get_attr_in_attrlist returns the attribute, but we need to check */
    /* if there is actually a value attached to it */

    if(o_wrappedkey_bytes && o_wrappedkey_bytes->pValue) { 
	

	/* BN_bin2bn works only with big endian, so we must alter data */
	/* if architecture is LE */
	
	*((CK_ULONG *)o_wrappedkey_bytes->pValue) = pkcs11_ll_bigendian_ul( *((CK_ULONG *)(o_wrappedkey_bytes->pValue))); /* transform if required */

	if ( (bn_wrappedkey_bytes = BN_bin2bn( o_wrappedkey_bytes->pValue, o_wrappedkey_bytes->ulValueLen, NULL)  ) == NULL ) {
	    P_ERR();
	    goto error;
	}
    } else { /* can be the case for CKK_DES, CKK_DES2 and CKK_DES3 family */
	     /* as these keys have no CKA_VALUE_LEN attribute */

	o_keytype = pkcs11_get_attr_in_attrlist(wrappedkey_attrs, CKA_KEY_TYPE);
	
	switch(*(CK_KEY_TYPE *)(o_keytype->pValue)) {
	case CKK_DES:
	    keysizeinbytes=8;
	    break;
	    
	case CKK_DES2:
	    keysizeinbytes=16;
	    break;
	    
	case CKK_DES3:
	    keysizeinbytes=24;
	    break;

	default:
	    fprintf(stderr,"***Error: unsupported key type for wrapping key\n");
	    rc = rc_error_unsupported;
	    goto error;}

	/* allocate BN */
	if ( (bn_wrappedkey_bytes = BN_new()) == NULL ) {
	    P_ERR();
	    goto error;
	}

	if ( BN_set_word(bn_wrappedkey_bytes, keysizeinbytes) == 0) {
	    P_ERR();
	    goto error;
	}
    }

    /* now check that len(wrapped_key) < len(wrapping_key) - 2 - 2 * hlen */
    /* !! lengths being expressed in bytes */
    /* in this version, Hash Algorithm set to SHA-1 and hardcoded */

    /* when SHA1, hlen=20, 2 * hlen + 2 = 42 */
    /* when SHA256, hlen=32,  2 * hlen + 2 = 66 */
    /* when SHA384, hlen=48,  2 * hlen + 2 = 98 */
    /* when SHA512, hlen=64,  2 * hlen + 2 = 130 */

    switch(wctx->oaep_params->hashAlg) {
    case CKM_SHA_1:
	sizeoverhead=42;
	break;

    case CKM_SHA256:
	sizeoverhead=66;
	break;

    case CKM_SHA384:
	sizeoverhead=98;
	break;

    case CKM_SHA512:
	sizeoverhead=130;
	break;

    default:
	fprintf(stderr,"***Error: unsupported hashing algorithm for OAEP wrapping\n");
	rc = rc_error_unsupported;
	goto error;
    }	
	

    if(! BN_add_word( bn_wrappedkey_bytes, sizeoverhead) ) {
	P_ERR();
	goto error;
    }

    /* if bn_wrapped_key  + sizeoverhead > bn_wrapping_key, then the wrapping key is too short.  */

    if( BN_cmp( bn_wrappedkey_bytes, bn_wrappingkey_bytes) > 0 ) {
	fprintf(stderr, "Error: wrapping key '%s' is too short to wrap key '%s'\n", wctx->wrappingkeylabel, wctx->wrappedkeylabel);
	rc = rc_error_wrapping_key_too_short;
	goto error;
    }


    /* we are good, let's allocate the memory and wrap */
    /* trick: we use now the CKA_MODULUS attribute to size the target buffer */

    wctx->wrapped_key_buffer = calloc ( o_modulus->ulValueLen, sizeof(unsigned char) );

    if(wctx->wrapped_key_buffer == NULL) {
	fprintf(stderr,"Error: memory\n");
	rc = rc_error_memory;
	goto error;
    }
    
    wctx->wrapped_key_len = o_modulus->ulValueLen;

    /* now wrap */

    {
	CK_RV rv;
	CK_MECHANISM mechanism = { CKM_RSA_PKCS_OAEP, wctx->oaep_params, sizeof(CK_RSA_PKCS_OAEP_PARAMS) };/* PKCS #1 OAEP wrap */

	rv = wctx->p11Context->FunctionList.C_WrapKey ( wctx->p11Context->Session,
							&mechanism,
							hWrappingKey,
							hWrappedKey,
							wctx->wrapped_key_buffer,
							&(wctx->wrapped_key_len) );
						 
	if(rv!=CKR_OK) {
	    pkcs11_error(rv, "C_WrapKey");
	    rc = rc_error_pkcs11_api;
	    goto error;
	}

	wctx->wrappedkeyhandle = hWrappedKey; /* keep a copy, for the output */
	wctx->wrappedkeyobjclass = CKO_SECRET_KEY; /* same story */
    }
   
error:
    if(bn_wrappingkey_bytes != NULL) { BN_free(bn_wrappingkey_bytes); bn_wrappingkey_bytes=NULL; }
    if(bn_wrappedkey_bytes != NULL) { BN_free(bn_wrappedkey_bytes); bn_wrappedkey_bytes=NULL; }
    pkcs11_delete_attrlist(wrappingkey_attrs);
    pkcs11_delete_attrlist(wrappedkey_attrs);

    return rc;
}



/*--------------------------------------------------------------------------------*/
/* PUBLIC INTERFACE                                                               */
/*--------------------------------------------------------------------------------*/


func_rc pkcs11_parse_wrappingalgorithm(wrappedKeyCtx *wctx, char *algostring)
{

    func_rc rc=rc_ok;

    if(wctx!=NULL && algostring!=NULL) {
	int parserc;
	
	/* http://stackoverflow.com/questions/1907847/how-to-use-yy-scan-string-in-lex     */
	/* copy string into new buffer and Switch buffers*/
	YY_BUFFER_STATE yybufstate = yy_scan_string(algostring);
	
	/* parse string */
	parserc = yyparse(wctx);

	if(parserc!=0) {
	    fprintf(stderr, "***Error scanning algorithm argument string '%s'\n", algostring);
	    rc =rc_error_invalid_argument;
	}
	
	/*Delete the new buffer*/
	yy_delete_buffer(yybufstate);
    } else {
	fprintf(stderr, "***Error: pkcs11_parse_wrappingalgoritm() called with wrong argument(s)\n");
	rc = rc_error_invalid_parameter_for_method;
    }
    
    return rc;
}



func_rc pkcs11_wrap(wrappedKeyCtx *wctx, char *wrappingkeylabel, char *wrappedkeylabel)
{
    func_rc rc = rc_ok;

    /* wctx at this point should have its wrapping_meth properly populated */
    wctx->wrappingkeylabel = strdup(wrappingkeylabel);
    wctx->wrappedkeylabel = strdup(wrappedkeylabel);
    
    switch(wctx->wrapping_meth) {
    case w_pkcs1_15:
	rc = _wrap_pkcs1_15(wctx);
	break;

    case w_pkcs1_oaep:
	rc = _wrap_pkcs1_oaep(wctx);
	break;

    case w_cbcpad:
	rc = _wrap_cbcpad(wctx);
	break;

    default:
	rc = rc_error_unknown_wrapping_alg;
	fprintf(stderr, "Error: unsupported wrapping algorithm.\n");
    }

    return rc;
}


func_rc pkcs11_output_wrapped_key( wrappedKeyCtx *wctx, char *filename )
{
    func_rc rc = rc_ok;
    FILE *fp=stdout;

    if(filename) {
	fp = fopen(filename, "w");
	if(fp==NULL) {
	    perror("***Warning: cannot write to file - will output to standard output");
	    fp=stdout;
	}
    }
    
    rc = _output_wrapped_key_header(wctx,fp);
    if(rc!=rc_ok) {
	fprintf(stderr, "Error during wrapped key header creation \n");
	goto error;		
    }
	    
    rc = _output_wrapped_key_attributes(wctx,fp);
    if(rc!=rc_ok) {
	fprintf(stderr, "Error during wrapped key attributes determination \n");
	goto error;
    }

    rc = _output_wrapped_key_b64(wctx,fp);
    if(rc!=rc_ok) {
	fprintf(stderr, "Error while outputing wrapped key\n");
	goto error;
    }

error:
    if(fp && fp!=stdout) {
	fclose(fp);
    }
    return rc;
}



