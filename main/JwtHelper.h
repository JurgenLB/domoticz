#pragma once

#include <string>
#include <set>
#include <system_error>
#include <json/json.h>

// Decoded JWT token information extracted from the token header and payload
struct JwtTokenInfo
{
	std::string algorithm;
	std::string subject;
	std::string key_id;
	std::set<std::string> audience;
	bool has_algorithm = false;
	bool has_audience = false;
	bool has_issuer = false;
	bool has_subject = false;
	bool has_key_id = false;
	bool has_expires_at = false;
	bool has_not_before = false;
	bool has_issued_at = false;
};

// Decode a JWT token and extract header/payload information.
// Returns false if the token is malformed and cannot be decoded.
bool JwtDecodeToken(const std::string &token, JwtTokenInfo &info);

// Verify a JWT token using a symmetric (HMAC) algorithm: HS256, HS384, or HS512.
// The ec output is set to a non-zero error code when verification fails.
bool JwtVerifyHmacToken(const std::string &token, const std::string &algorithm, const std::string &secret,
			const std::string &issuer, const std::string &audience, std::error_code &ec, int leeway_seconds = 60);

// Verify a JWT token using an asymmetric algorithm: RS256 or PS256.
// The ec output is set to a non-zero error code when verification fails.
bool JwtVerifyRsaToken(const std::string &token, const std::string &algorithm, const std::string &pubkey,
		       const std::string &issuer, const std::string &audience, std::error_code &ec, int leeway_seconds = 60);

// Create and sign a new JWT token using a symmetric (HMAC HS256) algorithm.
// extra_claims may contain string, numeric, or array-of-string payload claims.
// Returns the signed JWT string, or an empty string on failure.
std::string JwtSignHmacToken(const std::string &key_id, const std::string &issuer, const std::string &subject,
			     const std::string &audience, const std::string &jti, uint32_t exptime_seconds,
			     const Json::Value &extra_claims, const std::string &secret);

// Create and sign a new JWT token using an asymmetric (PS256) algorithm.
// extra_claims may contain string, numeric, or array-of-string payload claims.
// Returns the signed JWT string, or an empty string on failure.
std::string JwtSignRsaToken(const std::string &key_id, const std::string &issuer, const std::string &subject,
			    const std::string &audience, const std::string &jti, uint32_t exptime_seconds,
			    const Json::Value &extra_claims, const std::string &privkey);

// Create and sign a JWT token for Google Firebase Cloud Messaging (FCM).
// Returns the signed JWT string, or an empty string on failure.
std::string JwtCreateFCMToken(const std::string &issuer, const std::string &audience, const std::string &scope,
			      const std::string &privkey);
