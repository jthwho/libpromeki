# HttpApi: no authentication / authorization story

**File:** `include/promeki/httpapi.h:219` (`Endpoint`)

**FIXME(auth):** `HttpApi::Endpoint` has no notion of required
authentication or authorization scopes.  The catalog and
`/_openapi` endpoint cannot describe security requirements, and
the route installer cannot enforce them — every registered handler
is reachable by every caller.  The placeholder is tracked here so
this isn't silently forgotten when an auth story finally lands.

## Tasks

- [ ] Add a `security` field to `Endpoint` (a list of required
  scope / scheme names).
- [ ] Add `HttpApi::setSecurityScheme()` /
  `HttpApi::addSecurityScheme()` accessors that feed the OpenAPI
  `components.securitySchemes` block.
- [ ] Surface the per-endpoint `security[]` array in both the
  catalog JSON and the generated OpenAPI document.
- [ ] Make the route installer enforce the requirement by
  returning `401` / `403` *before* invoking the handler when
  authentication fails or required scopes are missing.
- [ ] Add tests covering: missing credentials → 401, present but
  insufficient scopes → 403, success path → handler runs and the
  OpenAPI doc / catalog reflect the scheme.
