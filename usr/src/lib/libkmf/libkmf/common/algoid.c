/*
 * Copyright (c) 1995-2000 Intel Corporation. All rights reserved.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdlib.h>
#include <string.h>

#include <kmfapiP.h>
#include <oidsalg.h>

typedef struct {
	KMF_OID * AlgOID;
	KMF_ALGORITHM_INDEX AlgID;
} KMF_OID_ID;

/*
 * The following table defines the mapping of AlgOID's to AlgID's.
 */
static KMF_OID_ID ALGOID_ID_Table[] = {
	{&KMFOID_X9CM_DSA, KMF_ALGID_DSA},
	{&KMFOID_X9CM_DSAWithSHA1, KMF_ALGID_SHA1WithDSA},
	{&KMFOID_SHA1, KMF_ALGID_SHA1},
	{&KMFOID_RSA, KMF_ALGID_RSA},
	{&KMFOID_DSA, KMF_ALGID_DSA},
	{&KMFOID_MD5WithRSA, KMF_ALGID_MD5WithRSA},
	{&KMFOID_MD2WithRSA, KMF_ALGID_MD2WithRSA},
	{&KMFOID_SHA1WithRSA, KMF_ALGID_SHA1WithRSA},
	{&KMFOID_SHA1WithDSA, KMF_ALGID_SHA1WithDSA}
};

#define	NUM_ALGOIDS ((sizeof (ALGOID_ID_Table))/(sizeof (ALGOID_ID_Table[0])))

/*
 * Name: X509_AlgIdToAlgorithmOid
 *
 * Description:
 * This function maps the specified AlgID to the corresponding
 * Algorithm OID.
 *
 * Parameters:
 * alg_int - AlgID to be mapped.
 *
 * Return value:
 * Pointer to OID structure and NULL in case of failure.
 *
 */
KMF_OID *
X509_AlgIdToAlgorithmOid(KMF_ALGORITHM_INDEX alg_int)
{
	int i;

	switch (alg_int) {
		case KMF_ALGID_NONE:
			return (NULL);

		default:
			for (i = 0; i < NUM_ALGOIDS; i++) {
				if (ALGOID_ID_Table[i].AlgID == alg_int)
					return (ALGOID_ID_Table[i].AlgOID);
			}
			break;
	}

	return (NULL);
}

/*
 * Name: X509_AlgorithmOidToAlgId
 *
 * Description:
 * This function maps the specified Algorithm OID to the corresponding
 * AlgID.
 *
 * Parameters:
 * Oid - OID to be mapped.
 *
 * Return value:
 * Algorithm ID and KMF_ALGID_NONE in case of failures.
 */
KMF_ALGORITHM_INDEX
X509_AlgorithmOidToAlgId(KMF_OID * Oid)
{
	int i;

	if ((Oid == NULL) ||
		(Oid->Data == NULL) ||
		(Oid->Length == 0)) {
		return (KMF_ALGID_NONE);
	}

	for (i = 0; i < NUM_ALGOIDS; i++) {
		if (IsEqualOid(ALGOID_ID_Table[i].AlgOID, Oid))
			return (ALGOID_ID_Table[i].AlgID);
	}

	return (KMF_ALGID_NONE);
}
