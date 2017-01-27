/* lillypass.c -- Passthrough for LDAPMessage chunks.
 *
 * This routine passes binary data into the lillyget_* routines, until it is
 * delivered.  At that point, it passes it back up, and delivers it to its
 * output stream.
 *
 * Coupling can be done at various levels, and this is why the number of
 * levels to pass through LDAP can be set as a first parameter; levels are:
 *
 *  0. Directly pass LDAPMessage chunks as a dercursor
 *  1. Pass a LDAPMessage after splitting into request, opcode and controls
 *  2. Pass LDAP operations with unpacked data, but use the same code for each
 *  3. Pass LDAP operations through individual operations (big risk of ENOSYS)
 *  4. The LDAP operations unpack the controls, and later pack them again
 *
 * TODO: level 3 still does what it did in lillydump.
 * TODO: level 4 has not been implemented yet.
 *
 * Reading / writing is highly structured, so it can be used for testing.
 * For this reason, query IDs and times will not be randomly generated.
 * Note that some operations may not be supported -- which is then reported.
 *
 * From: Rick van Rein <rick@openfortress.nl>
 */


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <errno.h>
#include <fcntl.h>

#define USE_SILLYMEM

#include <lillydap/api.h>
#include <lillydap/mem.h>

#include <quick-der/api.h>


int lillypass_BindRequest (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_BindRequest *br,
				const dercursor controls) {
	printf ("Got BindRequest\n");
	printf (" - version in %d bytes %02x,...\n", br->version.derlen, br->version.derptr [0]);
	printf (" - name \"%.*s\"\n", br->name.derlen, br->name.derptr);
	if (br->authentication.simple.derptr != NULL) {
		printf (" - simple authentication with \"%.*s\"\n", br->authentication.simple.derlen, br->authentication.simple.derptr);
	}
	if (br->authentication.sasl.mechanism.derptr != NULL) {
		printf (" - SASL mechanism \"%.*s\"\n", br->authentication.sasl.mechanism.derlen, br->authentication.sasl.mechanism.derptr);
		if (br->authentication.sasl.credentials.derptr != NULL) {
			printf (" - SASL credentias \"%.*s\"\n", br->authentication.sasl.credentials.derlen, br->authentication.sasl.credentials.derptr);
		}
	}
	return 0;
}

int lillypass_BindResponse (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_BindResponse *br,
				const dercursor controls) {
	printf ("Got BindResponse\n");
	printf (" - resultCode in %d bytes %02x,%02x,%02x,%02x,...\n", br->resultCode.derlen, br->resultCode.derptr [0], br->resultCode.derptr [1], br->resultCode.derptr [2], br->resultCode.derptr [3]);
	printf (" - matchedDN \"%.*s\"\n", br->matchedDN.derlen, br->matchedDN.derptr);
	printf (" - diagnosticMessage \"%.*s\"\n", br->diagnosticMessage.derlen, br->diagnosticMessage.derptr);
	return 0;
}

int lillypass_UnbindRequest (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_UnbindRequest *ur,
				const dercursor controls) {
	printf ("Got UnbindRequest\n");
	printf ("  - payload length is %s\n", (ur->derptr == NULL) ? "absent": (ur->derlen == 0) ? "empty" : "filled?!?");
	return 0;
}

int lillypass_SearchRequest (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_SearchRequest *sr,
				const dercursor controls) {
	printf ("Got SearchRequest\n");
	printf (" - baseObject \"%.*s\"\n", sr->baseObject.derlen, sr->baseObject.derptr);
	if (sr->scope.derlen != 1) {
		printf (" ? scope has awkward size %zd instead of 1\n", sr->scope.derlen);
	} else {
		switch (*sr->scope.derptr) {
		case 0:
			printf (" - scope base\n");
			break;
		case 1:
			printf (" - scope one\n");
			break;
		case 2:
			printf (" - scope sub\n");
			break;
		default:
			printf (" ? scope weird value %d instead of 0, 1 or 2\n", *sr->scope.derptr);
		}
	}
	if (sr->derefAliases.derlen != 1) {
		printf (" ? derefAliases has awkward size %zd instead of 1\n", sr->derefAliases.derlen);
	} else {
		switch (*sr->derefAliases.derptr) {
		case 0:
			printf (" - derefAliases neverDerefAlias\n");
			break;
		case 1:
			printf (" - derefAliases derefInSearching\n");
			break;
		case 2:
			printf (" - derefAliases derefFindingBaseObj\n");
			break;
		case 3:
			printf (" - derefAliases derefAlways\n");
			break;
		default:
			printf (" ? derefAliases weird value %d instead of 0, 1, 2 or 3\n", *sr->derefAliases.derptr);
		}
	}
	// attributes SEQUENCE OF LDAPString
	dercursor attrs = sr->attributes;
	printf (" - attributes.derlen = %zd\n", attrs.derlen);
	printf (" - attributes.enter.derlen = %zd\n", attrs.derlen);
	while (attrs.derlen > 0) {
		dercursor attr = attrs;
		if (der_focus (&attr)) {
			fprintf (stderr, "ERROR while focussing on attribute of SearchRequest: %s\n", strerror (errno));
		} else {
			printf (" - attr.derlen = %zd\n", attr.derlen);
			printf (" - attributes \"%.*s\"\n", attr.derlen, attr.derptr);
		}
		der_skip (&attrs);
	}
	return 0;
}

int lillypass_SearchResultEntry (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_SearchResultEntry *sre,
				const dercursor controls) {
	printf ("Got SearchResultEntry\n");
	printf (" - objectName \"%.*s\"\n", sre->objectName.derlen, sre->objectName.derptr);
	// partialAttribute SEQUENCE OF PartialAttribute
	dercursor pa = sre->attributes;
	der_enter (&pa);
	while (pa.derlen > 0) {
		dercursor type = pa;
		// SEQUENCE { type AttributeDescription,
		//		vals SET OF AttributeValue }
		der_enter (&type);
		printf (" - partialAttribute.type \"%.*s\"\n", type.derlen, type.derptr);
		der_skip (&pa);
		dercursor vals = pa;
		der_enter (&vals);
		while (vals.derlen > 0) {
			dercursor val = vals;
			der_enter (&val);
			printf ("    - value \"%.*s\"\n", val.derlen, val.derptr);
			der_skip (&vals);
		}
		der_skip (&pa);
	}
	return 0;
}

int lillypass_SearchResultReference (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_SearchResultReference *srr,
				const dercursor controls) {
	printf ("Got SearchResultReference\n");
	dercursor uris = *srr;
	do {
		dercursor uri = uris;
		der_enter (&uri);
		printf (" - URI \"%.*s\"\n", uri.derlen, uri.derptr);
		der_skip (&uris);
	} while (uris.derlen > 0);
	return 0;
}

int lillypass_SearchResultDone (LDAP *lil,
				LillyPool qpool,
				const LillyMsgId msgid,
				const LillyPack_SearchResultDone *srd,
				const dercursor controls) {
	printf ("Got SearchResultDone\n");
	printf (" - resultCode is %zd==1 byte valued %d\n", srd->resultCode.derlen, *srd->resultCode.derptr);
	printf (" - matchedDN \"%.*s\"\n", srd->matchedDN.derlen, srd->matchedDN.derptr);
	printf (" - diagnosticMessage \"%.*s\"\n", srd->diagnosticMessage.derlen, srd->diagnosticMessage.derptr);
	if (srd->referral.derptr != NULL) {
		dercursor uris = srd->referral;
		do {
			dercursor uri = uris;
			der_enter (&uri);
			printf (" - URI \"%.*s\"\n", uri.derlen, uri.derptr);
			der_skip (&uris);
		} while (uris.derlen > 0);
	}
	return 0;
}


void process (LDAP *lil, char *progname, char *derfilename) {
	//
	// Open the file
	int fd = open (derfilename, O_RDONLY);
	if (fd < 0) {
		fprintf (stderr, "%s: Failed to open \"%s\"\n", progname, derfilename);
		exit (1);
	}
	//
	// Print the file being handled
	//
	// Setup the input file descriptor
	int flags = fcntl (fd, F_GETFL, 0);
	if (flags == -1) {
		fprintf (stderr, "%s: Failed to get flags on stdin\n", progname);
		exit (1);
	}
	flags |= O_NONBLOCK;
	if (fcntl (fd, F_SETFL, flags) == -1) {
		fprintf (stderr, "%s: Failed to set non-blocking flag on stdin\n", progname);
		exit (1);
	}
	//
	// Set the file handle for input and output file handles in lil
	lil->get_fd = fd;
	lil->put_fd = 1;
	//
	// Send events until no more can be read
	int i;
	for (i=0; i<1000; i++) {
			lillyget_event (lil);
			lillyput_event (lil);
	}
	//
	// Close off processing
	close (fd);
}


void setup (void) {
	lillymem_newpool_fun = sillymem_newpool;
	lillymem_endpool_fun = sillymem_endpool;
	lillymem_alloc_fun   = sillymem_alloc;
}


static const LillyOpRegistry opregistry = {
	.by_name = {
		.BindRequest = lillypass_BindRequest,
		.BindResponse = lillypass_BindResponse,
		.UnbindRequest = lillypass_UnbindRequest,
		.SearchRequest = lillypass_SearchRequest,
		.SearchResultEntry = lillypass_SearchResultEntry,
		.SearchResultReference = lillypass_SearchResultReference,
		.SearchResultDone = lillypass_SearchResultDone,
	}
};


int main (int argc, char *argv []) {
	//
	// Check arguments
	char *progname = argv [0];
	if (argc < 3) {
		fprintf (stderr, "Usage: %s level ldapmsg.der...\nThe level is a value from 0 to 4, with increasing code being used\n", progname);
		exit (1);
	}
	//
	// Initialise functions and structures
	setup ();
	//
	// Create the memory pool
	LillyPool *lipo = lillymem_newpool ();
	if (lipo == NULL) {
		fprintf (stderr, "%s: Failed to allocate a memory pool\n", progname);
		exit (1);
	}
	//
	// Allocate the connection structuur
	LillyDAP *lil;
	lil = lillymem_alloc0 (lipo, sizeof (LillyDAP));
	//
	// We first setup all operations to pass over to output directly...
	lil->lillyget_dercursor   =
	lil->lillyput_dercursor   = lillyput_dercursor;
	lil->lillyget_ldapmessage =
	lil->lillyput_ldapmessage = lillyput_ldapmessage;
	lil->lillyget_operation   =
	lil->lillyput_operation   = lillyput_operation;
	//
	// ...and then we turn it back depending on the level
	char level = '\0';
	if (strlen (argv [1]) == 1) {
		level = argv [1] [0];
	}
	switch (level) {
	default:
		fprintf (stderr, "%s: Invalid level '%s'\n",
					argv [0], argv [1]);
		exit (1);
	case '4':
		fprintf (stderr, "%s: Level 4 is not yet implemented\n",
					argv [0]);
		// and fallthrough...
	case '3':
		lil->lillyget_operation   = lillyget_operation;
		// and fallthrough...
	case '2':
		lil->lillyget_ldapmessage = lillyget_ldapmessage;
		// and fallthrough...
	case '1':
		lil->lillyget_dercursor = lillyget_dercursor;
		// and fallthrough...
	case '0':
		// Keep everything as-is, passing as directly as possible
		break;
	}
	//
	// For level 4 we need the operation registry
	lil->opregistry = &opregistry;
	//
	// Allocate a connection pool
	lil->cnxpool = lillymem_newpool ();
	if (lil->cnxpool == NULL) {
		fprintf (stderr, "%s: Failed to allocate connection memory pool\n", progname);
		exit (1);
	}
	//
	// Iterate over the LDAP binary files in argv [1..]
	int argi = 2;
	while (argi < argc) {
		process (lil, progname, argv [argi]);
		argi++;
	}

	//
	// Cleanup and exit
	lillymem_endpool (lil->cnxpool);
	lillymem_endpool (lipo);
	exit (0);
}

