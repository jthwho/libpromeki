/**
 * @file      variant.tpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Out-of-class definitions of the heavy `VariantImpl` member functions
 * (`get<T>()` and `operator==`).  Deliberately **not** included from
 * variant.h: only variant.cpp includes it, so consumer translation units
 * never see the ~250-line get<T>() std::visit body or the 35²-branch
 * operator== visitor.  Together with the matching `extern template`
 * declarations at the bottom of variant.h, this routes all Variant
 * `.get<T>()` / `==` calls through one centrally-instantiated symbol
 * instead of re-instantiating them in every consumer TU.
 *
 * Every `T` appearing as the `To` parameter of `VariantImpl::get<T>()`
 * anywhere in the codebase is covered by `PROMEKI_VARIANT_TYPES` — the
 * X-macro in variant.h drives both the `extern template` declarations
 * and the explicit instantiations in variant.cpp.  New `.get<T>()`
 * call sites with a `T` outside that list must either add `T` to the
 * macro or include variant.tpp directly.
 */

#pragma once

#include <limits>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

template <typename... Types>
template <typename To>
To VariantImpl<Types...>::get(Error *err) const {
        return std::visit([err](auto &&arg) -> To {
                using From = std::decay_t<decltype(arg)>;
                if(err != nullptr) *err = Error::Ok;
                if constexpr (std::is_same_v<From, To>) {
                        return arg;

                } else if constexpr (std::is_same_v<To, bool>) {
                        if constexpr (std::is_integral<From>::value ||
                                std::is_floating_point<From>::value) return arg ? true : false;
                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);

                } else if constexpr (std::is_integral<To>::value) {
                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                        if constexpr (std::is_integral<From>::value ||
                                std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                        if constexpr (detail::is_type_registry_v<From>) return static_cast<To>(arg.id());
                        if constexpr (std::is_same_v<From, Enum>) return static_cast<To>(arg.value());
                        if constexpr (std::is_same_v<From, FrameNumber> ||
                                        std::is_same_v<From, FrameCount>) return static_cast<To>(arg.value());

                } else if constexpr (std::is_same_v<To, float>) {
                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                        if constexpr (std::is_integral<From>::value ||
                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();
                        if constexpr (std::is_same_v<From, FrameRate>) return static_cast<float>(arg.toDouble());
                        if constexpr (std::is_same_v<From, FrameNumber>) {
                                return arg.isValid() ? static_cast<float>(arg.value())
                                                     : std::numeric_limits<float>::quiet_NaN();
                        }
                        if constexpr (std::is_same_v<From, FrameCount>) {
                                if(arg.isUnknown())  return std::numeric_limits<float>::quiet_NaN();
                                if(arg.isInfinite()) return std::numeric_limits<float>::infinity();
                                return static_cast<float>(arg.value());
                        }

                } else if constexpr (std::is_same_v<To, double>) {
                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                        if constexpr (std::is_integral<From>::value ||
                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();
                        if constexpr (std::is_same_v<From, FrameRate>) return arg.toDouble();
                        if constexpr (std::is_same_v<From, FrameNumber>) {
                                return arg.isValid() ? static_cast<double>(arg.value())
                                                     : std::numeric_limits<double>::quiet_NaN();
                        }
                        if constexpr (std::is_same_v<From, FrameCount>) {
                                if(arg.isUnknown())  return std::numeric_limits<double>::quiet_NaN();
                                if(arg.isInfinite()) return std::numeric_limits<double>::infinity();
                                return static_cast<double>(arg.value());
                        }

                } else if constexpr (std::is_same_v<To, DateTime>) {
                        if constexpr (std::is_same_v<From, String>) return DateTime::fromString(
                                        arg, DateTime::DefaultFormat, err);

                } else if constexpr (std::is_same_v<To, UUID>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                UUID ret = UUID::fromString(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return UUID();
                                }
                                return ret;
                        }

                } else if constexpr (std::is_same_v<To, UMID>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                UMID ret = UMID::fromString(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return UMID();
                                }
                                return ret;
                        }

                } else if constexpr (std::is_same_v<To, Timecode>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Result<Timecode> ret = Timecode::fromString(arg);
                                if(ret.second().isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return Timecode();
                                }
                                return ret.first();
                        }


                } else if constexpr (std::is_same_v<To, FrameRate>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [fr, e] = FrameRate::fromString(arg);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return FrameRate();
                                }
                                return fr;
                        }
                        if constexpr (std::is_same_v<From, Rational<int>>) {
                                return FrameRate(FrameRate::RationalType(
                                        static_cast<unsigned int>(arg.numerator()),
                                        static_cast<unsigned int>(arg.denominator())));
                        }

                } else if constexpr (std::is_same_v<To, VideoFormat>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [vf, e] = VideoFormat::fromString(arg);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return VideoFormat();
                                }
                                return vf;
                        }

                } else if constexpr (std::is_same_v<To, StringList>) {
                        if constexpr (std::is_same_v<From, String>) return arg.split(",");

                } else if constexpr (std::is_same_v<To, Color>) {
                        if constexpr (std::is_same_v<From, String>) return Color::fromString(arg);

                } else if constexpr (std::is_same_v<To, ColorModel>) {
                        if constexpr (std::is_same_v<From, String>) return ColorModel::lookup(arg);
                        if constexpr (std::is_integral<From>::value) return ColorModel(static_cast<ColorModel::ID>(arg));

                } else if constexpr (std::is_same_v<To, MemSpace>) {
                        if constexpr (std::is_integral<From>::value) return MemSpace(static_cast<MemSpace::ID>(arg));

                } else if constexpr (std::is_same_v<To, PixelFormat>) {
                        if constexpr (std::is_same_v<From, String>) return PixelFormat::lookup(arg);
                        if constexpr (std::is_integral<From>::value) return PixelFormat(static_cast<PixelFormat::ID>(arg));

                } else if constexpr (std::is_same_v<To, PixelDesc>) {
                        if constexpr (std::is_same_v<From, String>) return PixelDesc::lookup(arg);
                        if constexpr (std::is_integral<From>::value) return PixelDesc(static_cast<PixelDesc::ID>(arg));

                } else if constexpr (std::is_same_v<To, VideoCodec>) {
                        if constexpr (std::is_same_v<From, String>) return VideoCodec::lookup(arg);
                        if constexpr (std::is_integral<From>::value) return VideoCodec(static_cast<VideoCodec::ID>(arg));

                } else if constexpr (std::is_same_v<To, AudioCodec>) {
                        if constexpr (std::is_same_v<From, String>) return AudioCodec::lookup(arg);
                        if constexpr (std::is_integral<From>::value) return AudioCodec(static_cast<AudioCodec::ID>(arg));

                } else if constexpr (std::is_same_v<To, Enum>) {
                        // Only String->Enum is supported; integer->Enum is intentionally
                        // disallowed because an Enum needs its type context, which a bare
                        // integer does not carry.  The consumer of the Variant must build
                        // the Enum themselves when they know the intended type.
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                Enum ret = Enum::lookup(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return Enum();
                                }
                                return ret;
                        }

                } else if constexpr (std::is_same_v<To, EnumList>) {
                        // EnumList can only be built from a String when the
                        // target element type is known — which the Variant
                        // layer does not know on its own.  Leave String->EnumList
                        // to VariantSpec::parseString (which has the spec's
                        // enumType in scope); here we only accept an explicit
                        // EnumList value that is already in the Variant.
                        (void)arg;

                } else if constexpr (std::is_same_v<To, MediaTimeStamp>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [mts, parseErr] = MediaTimeStamp::fromString(arg);
                                if(parseErr.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return MediaTimeStamp();
                                }
                                return mts;
                        }

                } else if constexpr (std::is_same_v<To, FrameNumber>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                FrameNumber fn = FrameNumber::fromString(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return FrameNumber::unknown();
                                }
                                return fn;
                        }
                        if constexpr (std::is_integral<From>::value) return FrameNumber(static_cast<int64_t>(arg));

                } else if constexpr (std::is_same_v<To, FrameCount>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                FrameCount fc = FrameCount::fromString(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return FrameCount::unknown();
                                }
                                return fc;
                        }
                        if constexpr (std::is_integral<From>::value) return FrameCount(static_cast<int64_t>(arg));

                } else if constexpr (std::is_same_v<To, MediaDuration>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                MediaDuration md = MediaDuration::fromString(arg, &e);
                                if(e.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return MediaDuration();
                                }
                                return md;
                        }

                } else if constexpr (std::is_same_v<To, Url>) {
                        if constexpr (std::is_same_v<From, String>) {
                                Error e;
                                Url u = Url::fromString(arg, &e);
                                if(e.isError() || !u.isValid()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return Url();
                                }
                                return u;
                        }

                } else if constexpr (std::is_same_v<To, String>) {
                        if constexpr (std::is_same_v<From, bool>) return String::number(arg);
                        if constexpr (std::is_same_v<From, int8_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, uint8_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, int16_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, uint16_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, int32_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, uint32_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, int64_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, uint64_t>) return String::number(arg);
                        if constexpr (std::is_same_v<From, float>) return String::number(arg);
                        if constexpr (std::is_same_v<From, double>) return String::number(arg);
                        if constexpr (std::is_same_v<From, DateTime>) return arg.toString();
                        if constexpr (std::is_same_v<From, TimeStamp>) return arg.toString();
                        if constexpr (std::is_same_v<From, MediaTimeStamp>) return arg.toString();
                        if constexpr (std::is_same_v<From, Size2Du32>) return arg.toString();
                        if constexpr (std::is_same_v<From, UUID>) return arg.toString();
                        if constexpr (std::is_same_v<From, UMID>) return arg.toString();
                        if constexpr (std::is_same_v<From, Timecode>) return arg.toString().first();
                        if constexpr (std::is_same_v<From, FrameNumber>) return arg.toString();
                        if constexpr (std::is_same_v<From, FrameCount>) return arg.toString();
                        if constexpr (std::is_same_v<From, MediaDuration>) return arg.toString();
                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toString();
                        if constexpr (std::is_same_v<From, FrameRate>) return arg.toString();
                        if constexpr (std::is_same_v<From, VideoFormat>) return arg.toString();
                        if constexpr (std::is_same_v<From, StringList>) return arg.join(",");
                        if constexpr (std::is_same_v<From, Color>) return arg.toString();
                        if constexpr (detail::is_type_registry_v<From>) return arg.name();
                        if constexpr (std::is_same_v<From, Enum>) return arg.toString();
                        if constexpr (std::is_same_v<From, EnumList>) return arg.toString();
                        if constexpr (std::is_same_v<From, Url>) return arg.toString();
#if PROMEKI_ENABLE_NETWORK
                        if constexpr (std::is_same_v<From, SocketAddress>) return arg.toString();
                        if constexpr (std::is_same_v<From, SdpSession>) return arg.toString();
                        if constexpr (std::is_same_v<From, MacAddress>) return arg.toString();
                        if constexpr (std::is_same_v<From, EUI64>) return arg.toString();
#endif

                }
#if PROMEKI_ENABLE_NETWORK
                else if constexpr (std::is_same_v<To, SocketAddress>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [addr, parseErr] = SocketAddress::fromString(arg);
                                if(parseErr.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return SocketAddress{};
                                }
                                return addr;
                        }
                } else if constexpr (std::is_same_v<To, SdpSession>) {
                        // Convert from String both for the
                        // case where a raw serialized SDP is
                        // stored in the Variant, and for the
                        // more common case of a file path —
                        // the parser recognises v=0 so a
                        // path with SDP content in it falls
                        // through cleanly to a parse error.
                        // Filesystem loading is up to the
                        // caller (use SdpSession::fromFile
                        // explicitly), so this path is only
                        // "parse from raw text".
                        if constexpr (std::is_same_v<From, String>) {
                                auto [sdp, parseErr] = SdpSession::fromString(arg);
                                if(parseErr.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return SdpSession{};
                                }
                                return sdp;
                        }
                } else if constexpr (std::is_same_v<To, MacAddress>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [mac, parseErr] = MacAddress::fromString(arg);
                                if(parseErr.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return MacAddress{};
                                }
                                return mac;
                        }
                } else if constexpr (std::is_same_v<To, EUI64>) {
                        if constexpr (std::is_same_v<From, String>) {
                                auto [eui, parseErr] = EUI64::fromString(arg);
                                if(parseErr.isError()) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return EUI64{};
                                }
                                return eui;
                        }
                }
#endif
                if(err != nullptr) *err = Error::Invalid;
                return To{};
        }, v);
}

template <typename... Types>
bool VariantImpl<Types...>::operator==(const VariantImpl &other) const {
        return std::visit([this, &other](auto &&a, auto &&b) -> bool {
                using A = std::decay_t<decltype(a)>;
                using B = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<A, B>) {
                        return a == b;
                } else if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
                        if constexpr (std::is_floating_point_v<A> || std::is_floating_point_v<B>) {
                                return static_cast<double>(a) == static_cast<double>(b);
                        } else if constexpr (std::is_signed_v<A> && !std::is_signed_v<B>) {
                                if(a < 0) return false;
                                return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
                        } else if constexpr (!std::is_signed_v<A> && std::is_signed_v<B>) {
                                if(b < 0) return false;
                                return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
                        } else {
                                using Common = std::common_type_t<A, B>;
                                return static_cast<Common>(a) == static_cast<Common>(b);
                        }
                } else {
                        Error err;
                        A ca = other.template get<A>(&err);
                        if(err.isOk() && a == ca) return true;
                        B cb = this->template get<B>(&err);
                        if(err.isOk() && cb == b) return true;
                        return false;
                }
        }, v, other.v);
}

PROMEKI_NAMESPACE_END
