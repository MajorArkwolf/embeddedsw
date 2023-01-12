/******************************************************************************
* Copyright (c) 2023, Xilinx, Inc.  All rights reserved.
* Copyright (c) 2023, Advanced Micro Devices, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
*******************************************************************************/

/*****************************************************************************/
/**
*
* @file xcert_genX509cert.c
*
* This file contains the implementation of the interface functions for creating
* X.509 certificate for DevIK and DevAK public key.
*
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date       Changes
* ----- ---- ---------- -------------------------------------------------------
* 1.0   har  01/09/2023 Initial release
*
* </pre>
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xil_util.h"
#include "xsecure_elliptic.h"
#include "xsecure_ellipticplat.h"
#include "xsecure_init.h"
#include "xsecure_sha384.h"
#include "xsecure_utils.h"
#include "xcert_genX509cert.h"
#include "xcert_createfield.h"

/************************** Constant Definitions *****************************/
#define XCERT_OID_SIGN_ALGO				"06082A8648CE3D040303"
#define XCERT_OID_EC_PUBLIC_KEY				"06072A8648CE3D0201"
#define XCERT_OID_P384					"06052B81040022"

#define XCERT_HASH_SIZE_IN_BYTES			(48U)
#define XCERT_SERIAL_FIELD_LEN				(22U)

/************************** Function Prototypes ******************************/
static void XCert_GenVersionField(u8* TBSCertBuf, u32 *VersionLen);
static void XCert_GenSerialField(u8* TBSCertBuf, u8* Serial, u32 *SerialLen);
static int XCert_GenSignAlgoField(u8* CertBuf, u32 *SignAlgoLen);
static int XCert_GenIssuerField(u8* TBSCertBuf, u8* Issuer, u32 *IssuerLen);
static int XCert_GenValidityField(u8* TBSCertBuf, u8* Validity, u32 *ValidityLen);
static int XCert_GenSubjectField(u8* TBSCertBuf, u8* Subject, u32 *SubjectLen);
static int XCert_GenPubKeyAlgIdentifierField(u8* TBSCertBuf, u32 *Len);
static int XCert_GenPublicKeyInfoField(u8* TBSCertBuf, u8* SubjectPublicKey,u32 *PubKeyInfoLen);
static int XCert_GenTBSCertificate(u8* X509CertBuf, XCert_Config Cfg, u32 *DataLen);
static int XCert_CalculateSign(u8* Data, u32 DataLen, XCert_Config Cfg, u8* Signature);
static void XCert_GenSignField(u8* X509CertBuf, u8* Signature, u32 *SignLen);

/************************** Function Definitions *****************************/
/*****************************************************************************/
/**
 * @brief	This function creates the X.509 Certificate.
 *
 * @param	X509CertBuf is the pointer to the X.509 Certificate buffer
 * @param	MaxCertSize is the maximum size of the X.509 Certificate buffer
 * @param	X509CertSize is the size of X.509 Certificate in bytes
 * @param	Cfg is structure which includes configuration for the X.509 Certificate.
 *
 * @note	Certificate  ::=  SEQUENCE  {
 *			tbsCertificate       TBSCertificate,
 *			signatureAlgorithm   AlgorithmIdentifier,
 *			signatureValue       BIT STRING  }
 *
 ******************************************************************************/
int XCert_GenerateX509Cert(u8* X509CertBuf, u32 MaxCertSize, u32* X509CertSize, XCert_Config Cfg)
{
	int Status = XST_FAILURE;
	u8* Start = X509CertBuf;
	u8* Curr = Start;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;
	u32 TBSCertLen;
	u32 SignAlgoLen;
	u32 SignLen;
	u8 Sign[XSECURE_ECC_P384_SIZE_IN_BYTES + XSECURE_ECC_P384_SIZE_IN_BYTES] = {0U};
	(void)MaxCertSize;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	/**
	 * Generate TBS certificate field
	 */
	Status = XCert_GenTBSCertificate(Curr, Cfg, &TBSCertLen);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + TBSCertLen;
	}

	/**
	 * Generate Sign Algorithm field
	 */
	Status = XCert_GenSignAlgoField(Curr, &SignAlgoLen);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + SignAlgoLen;
	}

	/**
	 * Calculate signature of the TBS certificate using the private key
	 */
	Status = XCert_CalculateSign(Start, TBSCertLen, Cfg, Sign);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/**
	 * Generate Signature field
	 */
	XCert_GenSignField(Curr, Sign, &SignLen);
	Curr = Curr + SignLen;

	/**
	 * Update the encoded length in the X.509 certificate SEQUENCE
	 */
	Status = XCert_UpdateEncodedLength(SequenceLenIdx, Curr - SequenceValIdx, SequenceValIdx);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	*X509CertSize = Curr - Start;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Version field of the TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Version field shall be added.
 * @param	VersionLen is the length of the Version field
 *
 * @note	Version  ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
 *		This field describes the version of the encoded certificate.
 *		XilCert library supports X.509 V3 certificates.
 *
 ******************************************************************************/
static void XCert_GenVersionField(u8* TBSCertBuf, u32 *VersionLen)
{
	u8* Curr = TBSCertBuf;

	*(Curr++) = XCERT_ASN1_TAG_INTEGER;
	*(Curr++) = XCERT_LEN_OF_VALUE_OF_VERSION;
	*(Curr++) = XCERT_VERSION_VALUE_V3;

	*VersionLen = Curr - TBSCertBuf;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Serial field of the TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Serial field shall be added.
 * @param	SerialLen is the length of the Serial field
 *
 * @note	CertificateSerialNumber  ::=  INTEGER
 *		The length of the serial must not be more than 20 bytes.
 *		The value of the serial is determined by calculating the
 *		SHA2 hash of the fields in the TBS Certificate except the Version
 *		and Serial Number fields. 20 bytes from LSB of the calculated
 *		hash is updated as the Serial Number
 *
 ******************************************************************************/
static void XCert_GenSerialField(u8* TBSCertBuf, u8* DataHash, u32 *SerialLen)
{
	u8 Serial[XCERT_LEN_OF_VALUE_OF_SERIAL] = {0U};

	XSecure_MemCpy64((u64)(UINTPTR)Serial, (u64)(UINTPTR)DataHash, XCERT_LEN_OF_VALUE_OF_SERIAL);

	XCert_CreateInteger(TBSCertBuf, Serial, XCERT_LEN_OF_VALUE_OF_SERIAL, SerialLen);
}

/*****************************************************************************/
/**
 * @brief	This function creates the Signature Algorithm field.
 *		This field in present in TBS Certificate as well as the
 *		X509 certificate.
 *
 * @param	CertBuf is the pointer in the Certificate buffer where
 *		the Signature Algorithm field shall be added.
 * @param	SignAlgoLen is the length of the SignAlgo field
 *
 * @note	AlgorithmIdentifier  ::=  SEQUENCE  {
 *		algorithm		OBJECT IDENTIFIER,
 *		parameters		ANY DEFINED BY algorithm OPTIONAL}
 *
 *		This function supports only ECDSA with SHA-384 as
 *		 Signature Algorithm.
 *		The algorithm identifier for ECDSA with SHA-384 signature values is:
 *		ecdsa-with-SHA384 OBJECT IDENTIFIER ::= { iso(1) member-body(2)
 *		us(840) ansi-X9-62(10045) signatures(4) ecdsa-with-SHA2(3) 3 }
 *		The parameters field MUST be absent.
 *		Hence, the AlgorithmIdentifier shall be a SEQUENCE of
 *		one component: the OID ecdsa-with-SHA384.
 *		The parameter field must be replaced with NULL.
 *
 ******************************************************************************/
static int XCert_GenSignAlgoField(u8* CertBuf, u32 *SignAlgoLen)
{
	int Status = XST_FAILURE;
	u8* Curr = CertBuf;
	u32 OidLen;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	Status = XCert_CreateRawData(Curr, (u8*)XCERT_OID_SIGN_ALGO, &OidLen);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + OidLen;
	}

	*(Curr++) = XCERT_ASN1_TAG_NULL;
	*(Curr++) = XCERT_NULL_VALUE;

	*SequenceLenIdx = Curr - SequenceValIdx;
	*SignAlgoLen = Curr - CertBuf;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Issuer field in TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Issuer field shall be added.
 * @param	Issuer is the DER encoded value of the Issuer field
 * @param	IssuerLen is the length of the Issuer field
 *
 * @note	This function expects the user to provide the Issuer field in DER
 *		encoded format and it will be updated in the TBS Certificate buffer.
 *
 ******************************************************************************/
static int XCert_GenIssuerField(u8* TBSCertBuf, u8* Issuer, u32 *IssuerLen)
{
	int Status = XST_FAILURE;

	Status = XCert_CreateRawData(TBSCertBuf, Issuer, IssuerLen);

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Validity field in TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Validity field shall be added.
 * @param	Validity is the DER encoded value of the Validity field
 * @param	ValidityLen is the length of the Validity field
 *
 * @note	This function expects the user to provide the Validity field in DER
 *		encoded format and it will be updated in the TBS Certificate buffer.
 *
 ******************************************************************************/
static int XCert_GenValidityField(u8* TBSCertBuf, u8* Validity, u32 *ValidityLen)
{
	int Status = XST_FAILURE;

	Status = XCert_CreateRawData(TBSCertBuf, Validity, ValidityLen);

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Subject field in TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Subject field shall be added.
 * @param	Subject is the DER encoded value of the Subject field
 * @param	SubjectLen is the length of the Subject field
 *
 * @note	This function expects the user to provide the Subject field in DER
 *		encoded format and it will be updated in the TBS Certificate buffer.
 *
 ******************************************************************************/
static int XCert_GenSubjectField(u8* TBSCertBuf, u8* Subject, u32 *SubjectLen)
{
	int Status = XST_FAILURE;

	Status = XCert_CreateRawData(TBSCertBuf, Subject, SubjectLen);

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Public Key Algorithm Identifier sub-field.
 *		It is a part of Subject Public Key Info field present in
 *		TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Public Key Algorithm Identifier sub-field shall be added.
 * @param	Len is the length of the Public Key Algorithm Identifier sub-field.
 *
 * @note	AlgorithmIdentifier  ::=  SEQUENCE  {
 *		algorithm		OBJECT IDENTIFIER,
 *		parameters		ANY DEFINED BY algorithm OPTIONAL}
 *
 *		This function supports only ECDSA P-384 key
 *		The algorithm identifier for ECDSA P-384 key is:
 *		{iso(1) member-body(2) us(840) ansi-x962(10045) keyType(2) ecPublicKey(1)}
 *		The parameter for ECDSA P-384 key is:
 *		secp384r1 OBJECT IDENTIFIER ::= {
 *		iso(1) identified-organization(3) certicom(132) curve(0) 34 }
 *
 *		Hence, the AlgorithmIdentifier shall be a SEQUENCE of
 *		two components: the OID id-ecPublicKey and OID secp384r1
 *
 ******************************************************************************/
static int XCert_GenPubKeyAlgIdentifierField(u8* TBSCertBuf, u32 *Len)
{
	int Status = XST_FAILURE;
	u8* Curr = TBSCertBuf;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;
	u32 OidLen;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	Status = XCert_CreateRawData(Curr, (u8*)XCERT_OID_EC_PUBLIC_KEY, &OidLen);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + OidLen;
	}

	Status = XCert_CreateRawData(Curr, (u8*)XCERT_OID_P384, &OidLen);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + OidLen;
	}

	*SequenceLenIdx = Curr - SequenceValIdx;
	*Len = Curr - TBSCertBuf;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Public Key Info field present in
 *		TBS Certificate.
 *
 * @param	TBSCertBuf is the pointer in the TBS Certificate buffer where
 *		the Public Key Info field shall be added.
 * @param	SubjectPublicKey is the public key of the Subject for which the
  *		certificate is being created.
 * @param	PubKeyInfoLen is the length of the Public Key Info field.
 *
 * @note	SubjectPublicKeyInfo  ::=  SEQUENCE  {
			algorithm            AlgorithmIdentifier,
			subjectPublicKey     BIT STRING  }
 *
 ******************************************************************************/
static int XCert_GenPublicKeyInfoField(u8* TBSCertBuf, u8* SubjectPublicKey, u32 *PubKeyInfoLen)
{
	int Status = XST_FAILURE;
	u32 KeyLen = XSECURE_ECC_P384_SIZE_IN_BYTES + XSECURE_ECC_P384_SIZE_IN_BYTES;
	u8* Curr = TBSCertBuf;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;
	u32 Len;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	Status = XCert_GenPubKeyAlgIdentifierField(Curr, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	Status = XCert_CreateBitString(Curr, SubjectPublicKey, KeyLen, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	*SequenceLenIdx = Curr - SequenceValIdx;
	*PubKeyInfoLen = Curr - TBSCertBuf;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the TBS(To Be Signed) Certificate.
 *
 * @param	TBSCertBuf is the pointer to the TBS Certificate buffer
 * @param	Cfg is structure which includes configuration for the TBS Certificate.
 * @param	TBSCertLen is the length of the TBS Certificate
 *
 * @note	TBSCertificate  ::=  SEQUENCE  {
 *			version         [0]  EXPLICIT Version DEFAULT v1,
 *			serialNumber         CertificateSerialNumber,
 *			signature            AlgorithmIdentifier,
 *			issuer               Name,
 *			validity             Validity,
 *			subject              Name,
 *			subjectPublicKeyInfo SubjectPublicKeyInfo,
 *		}
 *
 ******************************************************************************/
static int XCert_GenTBSCertificate(u8* TBSCertBuf, XCert_Config Cfg, u32 *TBSCertLen)
{
	int Status;
	u8* Start = TBSCertBuf;
	u8* Curr  = Start;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;
	u8* SerialStartIdx;
	u8 Hash[XCERT_HASH_SIZE_IN_BYTES] = {0U};
	u32 Len;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	/**
	 * Generate Version field
	 */
	XCert_GenVersionField(Curr, &Len);
	Curr = Curr + Len;

	/**
	 * Store the start index for the Serial field. Once all the remaining
	 * fields are populated then the SHA2 hash is calculated for the
	 * remaining fields in the TBS certificate and Serial is 20 bytes from
	 * LSB of the calculated hash. Hence we need to store the start index of Serial
	 * so that it can be updated later
	 */
	SerialStartIdx = Curr;

	/**
	 * Generate Signature Algorithm field
	 */
	Status = XCert_GenSignAlgoField(Curr, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	/**
	 * Generate Issuer field
	 */
	Status = XCert_GenIssuerField(Curr, Cfg.UserCfg.Issuer, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	/**
	 * Generate Validity field
	 */
	Status = XCert_GenValidityField(Curr, Cfg.UserCfg.Validity, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	/**
	 * Generate Subject field
	 */
	Status = XCert_GenSubjectField(Curr, Cfg.UserCfg.Subject, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	/**
	 * Generate Public Key Info field
	 */
	Status = XCert_GenPublicKeyInfoField(Curr, Cfg.AppCfg.SubjectPublicKey, &Len);
	if (Status != XST_SUCCESS) {
		goto END;
	}
	else {
		Curr = Curr + Len;
	}

	/**
	 * Calculate SHA2 Hash for all fields in the TBS certificate except Version and Serial
	 * Please note that currently SerialStartIdx points to the field after Serial.
	 * Hence this is the start pointer for calcualting the hash.
	 */
	Status = XSecure_Sha384Digest((u8* )SerialStartIdx, Curr - SerialStartIdx, Hash);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/**
	 * The fields after Serial have to be moved to make space for updating the Serial field
	 * Since the length of value of Serial field is 20 bytes and total length of Serial field
	 * (including tyag, length and value) is 22 bytes. So the remaining fields
	 * are moved by 22 bytes
	 */
	Status = Xil_SMemMove(SerialStartIdx + XCERT_SERIAL_FIELD_LEN, Curr - SerialStartIdx,
		SerialStartIdx, Curr - SerialStartIdx, Curr - SerialStartIdx);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/**
	 * Generate Serial field
	 */
	XCert_GenSerialField(SerialStartIdx, Hash, &Len);
	Curr = Curr + XCERT_SERIAL_FIELD_LEN;

	/**
	 * Update the encoded length in the TBS certificate SEQUENCE
	 */
	Status =  XCert_UpdateEncodedLength(SequenceLenIdx, Curr - SequenceValIdx, SequenceValIdx);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	*TBSCertLen = Curr - Start + 1;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function calcualtes the SHA2 hash of the TBS certificate
 *		and then signs it by using the provided private key.
 *
 * @param	Data is the pointer buffer which has to be signed
 * @param	DataLen is the length of the buffer which has to be signed.
 * @param	Cfg is structure which includes the required configuration
 * @param	Signature is the pointer to the buffer where the signature of the
 *		TBS certificate shall be stored.
 *
 * @note	For Dev IK self-signed certificates and DevAK certificates, DevIK
 *		private key should be provided for calculating the signature.
 *
 ******************************************************************************/
static int XCert_CalculateSign(u8* Data, u32 DataLen, XCert_Config Cfg, u8* Signature)
{
	int Status = XST_FAILURE;
	volatile int ClearStatus = XST_FAILURE;
	volatile int ClearStatusTmp = XST_FAILURE;
	u8 Hash[XCERT_HASH_SIZE_IN_BYTES] = {0U};
	u8 EphemeralKey[XSECURE_ECC_P384_SIZE_IN_BYTES] = {0U};
	u8 *SigR = Signature;
	u8 *SigS = &Signature[XSECURE_ECC_P384_SIZE_IN_BYTES];
	XSecure_EllipticSign Sign = {SigR, SigS};

	/**
	 * Calcualte SHA2 Digest of the TBS certificate
	 */
	Status = XSecure_Sha384Digest(Data, DataLen, Hash);
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/**
	 * Initialize TRNG to generate Ephemeral Key
	 */
	Status =  XSecure_TrngInit();
	if (Status != XST_SUCCESS) {
		goto END;
	}

	/**
	 * Generate Ephemeral Key using TRNG for generating ECDSA signature
	 */
	Status = XSecure_EllipticGenerateEphemeralKey(XSECURE_ECC_NIST_P384, (UINTPTR)&EphemeralKey);
	if (Status != XST_SUCCESS) {
		goto CLEAR;
	}

	/**
	 * Generate Signature using Private Key on the SHA2 hash of the TBS certificate
	 */
	Status = XSecure_EllipticGenerateSignature(XSECURE_ECC_NIST_P384, Hash,
		XSECURE_ECC_P384_SIZE_IN_BYTES, Cfg.AppCfg.PrvtKey, EphemeralKey, &Sign);
	if (Status != XST_SUCCESS) {
		goto CLEAR;
	}

CLEAR:
	/**
	 * Clear ephemeral key and private key
	 */
	ClearStatus = Xil_SecureZeroize(EphemeralKey, XSECURE_ECC_P384_SIZE_IN_BYTES);
	ClearStatusTmp = Xil_SecureZeroize(EphemeralKey, XSECURE_ECC_P384_SIZE_IN_BYTES);
	if ((ClearStatus != XST_SUCCESS) || (ClearStatusTmp != XST_SUCCESS)) {
		Status = (ClearStatus | ClearStatusTmp);
	}

	ClearStatus = Xil_SecureZeroize(Cfg.AppCfg.PrvtKey, XSECURE_ECC_P384_SIZE_IN_BYTES);
	ClearStatusTmp = Xil_SecureZeroize(Cfg.AppCfg.PrvtKey, XSECURE_ECC_P384_SIZE_IN_BYTES);
	if ((ClearStatus != XST_SUCCESS) || (ClearStatusTmp != XST_SUCCESS)) {
		Status = (ClearStatus | ClearStatusTmp);
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function creates the Signature field in the X.509 certificate
 *
 * @param	X509CertBuf is the pointer to the X.509 Certificate buffer
 * @param	Signature is value of the Signature field in X.509 certificate
 * @param	SignLen is the length of the Signature field
 *
 * @note	Th signature value is encoded as a BIT STRING and included in the
 *		signature field. When signing, the ECDSA algorithm generates
 *		two values - r and s.
 *		To easily transfer these two values as one signature,
 *		they MUST be DER encoded using the following ASN.1 structure:
 *		Ecdsa-Sig-Value  ::=  SEQUENCE  {
 *			r     INTEGER,
 *			s     INTEGER  }
 *
 ******************************************************************************/
static void XCert_GenSignField(u8* X509CertBuf, u8* Signature, u32 *SignLen)
{
	u8* Curr = X509CertBuf;
	u8* BitStrLenIdx;
	u8* BitStrValIdx;
	u8* SequenceLenIdx;
	u8* SequenceValIdx;
	u32 Len;

	*(Curr++) = XCERT_ASN1_TAG_BITSTRING;
	BitStrLenIdx = Curr++;
	BitStrValIdx = Curr++;
	*BitStrValIdx = 0x00;

	*(Curr++) = XCERT_ASN1_TAG_SEQUENCE;
	SequenceLenIdx = Curr++;
	SequenceValIdx = Curr;

	XCert_CreateInteger(Curr, Signature, XSECURE_ECC_P384_SIZE_IN_BYTES, &Len);
	Curr = Curr + Len;

	XCert_CreateInteger(Curr, Signature + XSECURE_ECC_P384_SIZE_IN_BYTES,
		XSECURE_ECC_P384_SIZE_IN_BYTES, &Len);
	Curr = Curr + Len;

	*SequenceLenIdx = Curr - SequenceValIdx;
	*BitStrLenIdx = Curr - BitStrValIdx;
	*SignLen = Curr - X509CertBuf;
}