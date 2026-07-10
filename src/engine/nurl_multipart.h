#ifndef NURL_MULTIPART_H
#define NURL_MULTIPART_H

#include "engine/nurl_engine_request.h"

typedef struct NutMultipart NutMultipart;

NutMultipart *nurl_multipart_new(void);
void nurl_multipart_add_file(NutMultipart *m, const char *field_name,
                             const char *filepath, const char *mime_type);
void nurl_multipart_add_field(NutMultipart *m, const char *name, const char *value);

// Returns Content-Type header value ("multipart/form-data; boundary=...")
const char *nurl_multipart_content_type(const NutMultipart *m);

void nurl_multipart_into_request(NutMultipart *m, NutRequest *req);
void nurl_multipart_free(NutMultipart *m);

#endif /* NURL_MULTIPART_H */
