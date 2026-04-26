/**
 * @file      httpstatus.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/enum.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Well-known Enum type for HTTP response status codes.
 * @ingroup network
 *
 * The integer value is the actual numeric status code sent on the wire
 * (200, 404, 500, …) — not an opaque internal ID — so
 * @c HttpStatus::Ok.value() == 200.  This makes it cheap to round-trip
 * to/from llhttp and from JSON APIs that expose the status as an
 * integer.
 *
 * Use the named constants where possible for readability; raw integers
 * are accepted for codes outside the well-known set
 * (e.g. @c HttpStatus(418)).  @ref reasonPhrase returns the canonical
 * RFC 9110 phrase for the code, or @c "Status <code>" as a fallback for
 * unrecognized codes so the wire form remains valid.
 *
 * Default value is @ref Ok — the most common success code, mirroring
 * the convention chosen for @ref HttpMethod.
 *
 * @par Example
 * @code
 * HttpStatus s = HttpStatus::NotFound;
 * int code = s.value();              // 404
 * String phrase = s.reasonPhrase();  // "Not Found"
 *
 * HttpStatus custom{418};            // I'm a teapot — not in the table
 * custom.reasonPhrase();             // "I'm a Teapot"
 * @endcode
 */
class HttpStatus : public TypedEnum<HttpStatus> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE(
                        "HttpStatus", 200,
                        // 1xx Informational
                        {"Continue", 100}, {"SwitchingProtocols", 101}, {"Processing", 102}, {"EarlyHints", 103},

                        // 2xx Success
                        {"Ok", 200}, {"Created", 201}, {"Accepted", 202}, {"NonAuthoritativeInformation", 203},
                        {"NoContent", 204}, {"ResetContent", 205}, {"PartialContent", 206},

                        // 3xx Redirection
                        {"MultipleChoices", 300}, {"MovedPermanently", 301}, {"Found", 302}, {"SeeOther", 303},
                        {"NotModified", 304}, {"TemporaryRedirect", 307}, {"PermanentRedirect", 308},

                        // 4xx Client errors
                        {"BadRequest", 400}, {"Unauthorized", 401}, {"PaymentRequired", 402}, {"Forbidden", 403},
                        {"NotFound", 404}, {"MethodNotAllowed", 405}, {"NotAcceptable", 406},
                        {"ProxyAuthenticationRequired", 407}, {"RequestTimeout", 408}, {"Conflict", 409}, {"Gone", 410},
                        {"LengthRequired", 411}, {"PreconditionFailed", 412}, {"PayloadTooLarge", 413},
                        {"UriTooLong", 414}, {"UnsupportedMediaType", 415}, {"RangeNotSatisfiable", 416},
                        {"ExpectationFailed", 417}, {"ImATeapot", 418}, {"MisdirectedRequest", 421},
                        {"UnprocessableEntity", 422}, {"Locked", 423}, {"FailedDependency", 424}, {"TooEarly", 425},
                        {"UpgradeRequired", 426}, {"PreconditionRequired", 428}, {"TooManyRequests", 429},
                        {"RequestHeaderFieldsTooLarge", 431}, {"UnavailableForLegalReasons", 451},

                        // 5xx Server errors
                        {"InternalServerError", 500}, {"NotImplemented", 501}, {"BadGateway", 502},
                        {"ServiceUnavailable", 503}, {"GatewayTimeout", 504}, {"HttpVersionNotSupported", 505},
                        {"VariantAlsoNegotiates", 506}, {"InsufficientStorage", 507}, {"LoopDetected", 508},
                        {"NotExtended", 510}, {"NetworkAuthenticationRequired", 511});

                using TypedEnum<HttpStatus>::TypedEnum;

                // 1xx
                static const HttpStatus Continue;
                static const HttpStatus SwitchingProtocols;
                static const HttpStatus Processing;
                static const HttpStatus EarlyHints;

                // 2xx
                static const HttpStatus Ok;
                static const HttpStatus Created;
                static const HttpStatus Accepted;
                static const HttpStatus NonAuthoritativeInformation;
                static const HttpStatus NoContent;
                static const HttpStatus ResetContent;
                static const HttpStatus PartialContent;

                // 3xx
                static const HttpStatus MultipleChoices;
                static const HttpStatus MovedPermanently;
                static const HttpStatus Found;
                static const HttpStatus SeeOther;
                static const HttpStatus NotModified;
                static const HttpStatus TemporaryRedirect;
                static const HttpStatus PermanentRedirect;

                // 4xx
                static const HttpStatus BadRequest;
                static const HttpStatus Unauthorized;
                static const HttpStatus PaymentRequired;
                static const HttpStatus Forbidden;
                static const HttpStatus NotFound;
                static const HttpStatus MethodNotAllowed;
                static const HttpStatus NotAcceptable;
                static const HttpStatus ProxyAuthenticationRequired;
                static const HttpStatus RequestTimeout;
                static const HttpStatus Conflict;
                static const HttpStatus Gone;
                static const HttpStatus LengthRequired;
                static const HttpStatus PreconditionFailed;
                static const HttpStatus PayloadTooLarge;
                static const HttpStatus UriTooLong;
                static const HttpStatus UnsupportedMediaType;
                static const HttpStatus RangeNotSatisfiable;
                static const HttpStatus ExpectationFailed;
                static const HttpStatus ImATeapot;
                static const HttpStatus MisdirectedRequest;
                static const HttpStatus UnprocessableEntity;
                static const HttpStatus Locked;
                static const HttpStatus FailedDependency;
                static const HttpStatus TooEarly;
                static const HttpStatus UpgradeRequired;
                static const HttpStatus PreconditionRequired;
                static const HttpStatus TooManyRequests;
                static const HttpStatus RequestHeaderFieldsTooLarge;
                static const HttpStatus UnavailableForLegalReasons;

                // 5xx
                static const HttpStatus InternalServerError;
                static const HttpStatus NotImplemented;
                static const HttpStatus BadGateway;
                static const HttpStatus ServiceUnavailable;
                static const HttpStatus GatewayTimeout;
                static const HttpStatus HttpVersionNotSupported;
                static const HttpStatus VariantAlsoNegotiates;
                static const HttpStatus InsufficientStorage;
                static const HttpStatus LoopDetected;
                static const HttpStatus NotExtended;
                static const HttpStatus NetworkAuthenticationRequired;

                /**
                 * @brief Returns the canonical RFC 9110 reason phrase.
                 *
                 * Produces the wire-form text that follows the status
                 * code on the response status line ("OK", "Not Found",
                 * "Internal Server Error", ...).  Falls back to
                 * @c "Status <code>" for codes not in the well-known
                 * table so an arbitrary integer status still produces
                 * a syntactically-valid HTTP response.
                 */
                String reasonPhrase() const;

                /** @brief 1xx informational. */
                bool isInformational() const { return value() >= 100 && value() < 200; }
                /** @brief 2xx success. */
                bool isSuccess() const { return value() >= 200 && value() < 300; }
                /** @brief 3xx redirection. */
                bool isRedirect() const { return value() >= 300 && value() < 400; }
                /** @brief 4xx client error. */
                bool isClientError() const { return value() >= 400 && value() < 500; }
                /** @brief 5xx server error. */
                bool isServerError() const { return value() >= 500 && value() < 600; }
                /** @brief Convenience: 4xx or 5xx. */
                bool isError() const { return isClientError() || isServerError(); }
};

inline const HttpStatus HttpStatus::Continue{100};
inline const HttpStatus HttpStatus::SwitchingProtocols{101};
inline const HttpStatus HttpStatus::Processing{102};
inline const HttpStatus HttpStatus::EarlyHints{103};
inline const HttpStatus HttpStatus::Ok{200};
inline const HttpStatus HttpStatus::Created{201};
inline const HttpStatus HttpStatus::Accepted{202};
inline const HttpStatus HttpStatus::NonAuthoritativeInformation{203};
inline const HttpStatus HttpStatus::NoContent{204};
inline const HttpStatus HttpStatus::ResetContent{205};
inline const HttpStatus HttpStatus::PartialContent{206};
inline const HttpStatus HttpStatus::MultipleChoices{300};
inline const HttpStatus HttpStatus::MovedPermanently{301};
inline const HttpStatus HttpStatus::Found{302};
inline const HttpStatus HttpStatus::SeeOther{303};
inline const HttpStatus HttpStatus::NotModified{304};
inline const HttpStatus HttpStatus::TemporaryRedirect{307};
inline const HttpStatus HttpStatus::PermanentRedirect{308};
inline const HttpStatus HttpStatus::BadRequest{400};
inline const HttpStatus HttpStatus::Unauthorized{401};
inline const HttpStatus HttpStatus::PaymentRequired{402};
inline const HttpStatus HttpStatus::Forbidden{403};
inline const HttpStatus HttpStatus::NotFound{404};
inline const HttpStatus HttpStatus::MethodNotAllowed{405};
inline const HttpStatus HttpStatus::NotAcceptable{406};
inline const HttpStatus HttpStatus::ProxyAuthenticationRequired{407};
inline const HttpStatus HttpStatus::RequestTimeout{408};
inline const HttpStatus HttpStatus::Conflict{409};
inline const HttpStatus HttpStatus::Gone{410};
inline const HttpStatus HttpStatus::LengthRequired{411};
inline const HttpStatus HttpStatus::PreconditionFailed{412};
inline const HttpStatus HttpStatus::PayloadTooLarge{413};
inline const HttpStatus HttpStatus::UriTooLong{414};
inline const HttpStatus HttpStatus::UnsupportedMediaType{415};
inline const HttpStatus HttpStatus::RangeNotSatisfiable{416};
inline const HttpStatus HttpStatus::ExpectationFailed{417};
inline const HttpStatus HttpStatus::ImATeapot{418};
inline const HttpStatus HttpStatus::MisdirectedRequest{421};
inline const HttpStatus HttpStatus::UnprocessableEntity{422};
inline const HttpStatus HttpStatus::Locked{423};
inline const HttpStatus HttpStatus::FailedDependency{424};
inline const HttpStatus HttpStatus::TooEarly{425};
inline const HttpStatus HttpStatus::UpgradeRequired{426};
inline const HttpStatus HttpStatus::PreconditionRequired{428};
inline const HttpStatus HttpStatus::TooManyRequests{429};
inline const HttpStatus HttpStatus::RequestHeaderFieldsTooLarge{431};
inline const HttpStatus HttpStatus::UnavailableForLegalReasons{451};
inline const HttpStatus HttpStatus::InternalServerError{500};
inline const HttpStatus HttpStatus::NotImplemented{501};
inline const HttpStatus HttpStatus::BadGateway{502};
inline const HttpStatus HttpStatus::ServiceUnavailable{503};
inline const HttpStatus HttpStatus::GatewayTimeout{504};
inline const HttpStatus HttpStatus::HttpVersionNotSupported{505};
inline const HttpStatus HttpStatus::VariantAlsoNegotiates{506};
inline const HttpStatus HttpStatus::InsufficientStorage{507};
inline const HttpStatus HttpStatus::LoopDetected{508};
inline const HttpStatus HttpStatus::NotExtended{510};
inline const HttpStatus HttpStatus::NetworkAuthenticationRequired{511};

PROMEKI_NAMESPACE_END
