#include "stdafx.h"
#include "JwtHelper.h"
#include "../webserver/Base64.h"
#include <algorithm>
#include <vector>

#define JWT_DISABLE_BASE64
#include <jwt-cpp/jwt.h>

// Helper to apply HMAC algorithm to a JWT verifier
template <typename Verifier>
static void ApplyHmacAlgorithm(Verifier &verifier, const std::string &algorithm, const std::string &secret)
{
	if (algorithm == "HS256")
		verifier.allow_algorithm(jwt::algorithm::hs256{ secret });
	else if (algorithm == "HS384")
		verifier.allow_algorithm(jwt::algorithm::hs384{ secret });
	else if (algorithm == "HS512")
		verifier.allow_algorithm(jwt::algorithm::hs512{ secret });
}

bool JwtDecodeToken(const std::string &token, JwtTokenInfo &info)
{
	try
	{
		auto decoded = jwt::decode(token, &base64url_decode);
		info.has_algorithm = decoded.has_algorithm();
		if (info.has_algorithm)
			info.algorithm = decoded.get_algorithm();
		info.has_audience = decoded.has_audience();
		if (info.has_audience)
		{
			for (const auto &aud : decoded.get_audience())
				info.audience.insert(aud);
		}
		info.has_issuer = decoded.has_issuer();
		info.has_subject = decoded.has_subject();
		if (info.has_subject)
			info.subject = decoded.get_subject();
		info.has_key_id = decoded.has_key_id();
		if (info.has_key_id)
			info.key_id = decoded.get_key_id();
		info.has_expires_at = decoded.has_expires_at();
		info.has_not_before = decoded.has_not_before();
		info.has_issued_at = decoded.has_issued_at();
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool JwtVerifyHmacToken(const std::string &token, const std::string &algorithm, const std::string &secret,
			const std::string &issuer, const std::string &audience, std::error_code &ec, int leeway_seconds)
{
	try
	{
		auto decoded = jwt::decode(token, &base64url_decode);
		auto verifier = jwt::verify().with_issuer(issuer).with_audience(audience);
		ApplyHmacAlgorithm(verifier, algorithm, secret);
		verifier.expires_at_leeway(leeway_seconds);
		verifier.not_before_leeway(leeway_seconds);
		verifier.issued_at_leeway(leeway_seconds);
		verifier.verify(decoded, ec);
		return !ec;
	}
	catch (...)
	{
		ec = std::make_error_code(std::errc::invalid_argument);
		return false;
	}
}

bool JwtVerifyRsaToken(const std::string &token, const std::string &algorithm, const std::string &pubkey,
		       const std::string &issuer, const std::string &audience, std::error_code &ec, int leeway_seconds)
{
	try
	{
		auto decoded = jwt::decode(token, &base64url_decode);
		auto verifier = jwt::verify().with_issuer(issuer).with_audience(audience);
		if (algorithm == "RS256")
			verifier.allow_algorithm(jwt::algorithm::rs256{ pubkey });
		else if (algorithm == "PS256")
			verifier.allow_algorithm(jwt::algorithm::ps256{ pubkey });
		verifier.expires_at_leeway(leeway_seconds);
		verifier.not_before_leeway(leeway_seconds);
		verifier.issued_at_leeway(leeway_seconds);
		verifier.verify(decoded, ec);
		return !ec;
	}
	catch (...)
	{
		ec = std::make_error_code(std::errc::invalid_argument);
		return false;
	}
}

// Apply extra claims from a Json::Value to a JWT builder
template <typename Builder>
static void ApplyExtraClaims(Builder &builder, const Json::Value &extra_claims)
{
	if (extra_claims.empty())
		return;
	for (auto const &id : extra_claims.getMemberNames())
	{
		if (extra_claims[id].isNull())
			continue;
		if (extra_claims[id].isNumeric())
		{
			double dVal(extra_claims[id].asDouble());
			builder.set_payload_claim(id, picojson::value(dVal));
		}
		else if (extra_claims[id].isString())
		{
			std::string sVal(extra_claims[id].asString());
			builder.set_payload_claim(id, picojson::value(sVal));
		}
		else if (extra_claims[id].isArray())
		{
			std::vector<std::string> aStrList;
			aStrList.reserve(extra_claims[id].size());
			std::transform(extra_claims[id].begin(), extra_claims[id].end(), std::back_inserter(aStrList), [](const auto &s) { return s.asString(); });
			builder.set_payload_claim(id, jwt::claim(aStrList.begin(), aStrList.end()));
		}
	}
}

std::string JwtSignHmacToken(const std::string &key_id, const std::string &issuer, const std::string &subject,
			     const std::string &audience, const std::string &jti, uint32_t exptime_seconds,
			     const Json::Value &extra_claims, const std::string &secret)
{
	try
	{
		auto builder = jwt::create()
				       .set_type("JWT")
				       .set_key_id(key_id)
				       .set_issuer(issuer)
				       .set_issued_at(std::chrono::system_clock::now())
				       .set_not_before(std::chrono::system_clock::now())
				       .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{ exptime_seconds })
				       .set_audience(audience)
				       .set_subject(subject)
				       .set_id(jti);
		ApplyExtraClaims(builder, extra_claims);
		return builder.sign(jwt::algorithm::hs256{ secret }, &base64url_encode);
	}
	catch (...)
	{
		return {};
	}
}

std::string JwtSignRsaToken(const std::string &key_id, const std::string &issuer, const std::string &subject,
			    const std::string &audience, const std::string &jti, uint32_t exptime_seconds,
			    const Json::Value &extra_claims, const std::string &privkey)
{
	try
	{
		auto builder = jwt::create()
				       .set_type("JWT")
				       .set_key_id(key_id)
				       .set_issuer(issuer)
				       .set_issued_at(std::chrono::system_clock::now())
				       .set_not_before(std::chrono::system_clock::now())
				       .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{ exptime_seconds })
				       .set_audience(audience)
				       .set_subject(subject)
				       .set_id(jti);
		ApplyExtraClaims(builder, extra_claims);
		return builder.sign(jwt::algorithm::ps256{ "", privkey, "", "" }, &base64url_encode);
	}
	catch (...)
	{
		return {};
	}
}

std::string JwtCreateFCMToken(const std::string &issuer, const std::string &audience, const std::string &scope,
			      const std::string &privkey)
{
	try
	{
		auto builder = jwt::create()
				       .set_type("JWT")
				       .set_issuer(issuer)
				       .set_audience(audience)
				       .set_issued_at(std::chrono::system_clock::now())
				       .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{ 600 })
				       .set_payload_claim("scope", jwt::claim(std::string{ scope }));
		return builder.sign(jwt::algorithm::rs256{ "", privkey, "", "" }, &base64url_encode);
	}
	catch (...)
	{
		return {};
	}
}
