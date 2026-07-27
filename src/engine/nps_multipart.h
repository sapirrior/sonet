#ifndef NPS_MULTIPART_H
#define NPS_MULTIPART_H

#include "engine/nps_engine_request.h"

typedef struct NpsMultipart NpsMultipart;

NpsMultipart *nps_multipart_new(void);
void nps_multipart_add_file(NpsMultipart *m, const char *field_name,
                             const char *filepath, const char *mime_type);
void nps_multipart_add_field(NpsMultipart *m, const char *name, const char *value);

// Returns Content-Type header value ("multipart/form-data; boundary=...")
const char *nps_multipart_content_type(const NpsMultipart *m);

void nps_multipart_into_request(NpsMultipart *m, NpsRequest *req);
void nps_multipart_free(NpsMultipart *m);

#endif /* NPS_MULTIPART_H */
