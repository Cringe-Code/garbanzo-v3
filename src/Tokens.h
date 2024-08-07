#pragma once

#include <jwt-cpp/jwt.h>
#include <drogon/drogon.h>
#include <iostream>
#include "Base.h"
#include "jwt-cpp/traits/kazuho-picojson/defaults.h"
#include <bcrypt.h>

class Tokens {
public:
    Tokens(std::string access = "", std::string refresh = "") 
    : Access(std::move(access)), Refresh(std::move(refresh)) 
    {}

    static std::string generate_jwt_access_token () {
        auto token = jwt::create()
            .set_type("JWS")
            .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{600})
            .sign(jwt::algorithm::hs256{SECRET_KEY});
        return token;
    }

    static std::string generate_jwt_refresh_token(const std::string &user_id, const std::string &deviceId) {
        auto token = jwt::create()
            .set_type("JWS")
            .set_payload_claim("user_id", jwt::claim(user_id))
            .set_payload_claim("device_id", jwt::claim(deviceId))
            .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{2592000})
            .sign(jwt::algorithm::hs256{SECRET_KEY});
        return token;
    }

    inline std::pair<Tokens, std::string> update_tokens (const std::string &deviceId, const drogon::orm::DbClientPtr &dbClient) {
        Tokens t = {
            Access,
            Refresh
        };

        auto val = validate_access_token(t.Access);
        auto val1 = validate_refresh_token(t.Refresh, dbClient, deviceId);

        if (val.first) {
            return {{Access, Refresh}, val1.second};
        }
        else {
            if (!val1.first) { // если refresh token невалидный, то юзер не получит новую пару токенов
                return {{"", ""}, ""};
            }
            std::string new_access = generate_jwt_access_token();

            dbClient->execSqlSync("update tokens set refresh_token=$1 where user_id=$2", 
                bcrypt::generateHash(t.Refresh), val1.second);

            return {{new_access, t.Refresh}, val1.second};
        }
    }

    std::string Access;
    std::string Refresh;
private:

    static std::pair<bool, std::string> validate_access_token (std::string &access_token) {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{SECRET_KEY})
            .expires_at_leeway(60); 

        auto decoded_jwt = jwt::decode(access_token);

        try {
            verifier.verify(decoded_jwt);
        }
        catch (const std::exception& exc) {
            std::cerr << "Error while check access token " << exc.what() << "\n";
            return {false, ""};
        }
        
        return {true, access_token};
    }

    static std::pair<bool, std::string> validate_refresh_token (std::string &refresh_token, const drogon::orm::DbClientPtr &dbClient,
        const std::string &deviceId) {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{SECRET_KEY})
            .expires_at_leeway(2592000); 

        auto decoded_jwt = jwt::decode(refresh_token);

        try {
            verifier.verify(decoded_jwt);
        }
        catch (const std::exception& exc) {
            std::cerr << "Error while check refresh token: " << exc.what() << "\n";
            return {false, ""};
        }

        auto user_id = decoded_jwt.get_payload_claim("user_id").as_string();

        auto rows = dbClient->execSqlSync("select refresh_token from tokens where user_id=$1 and device_id=$2", 
            user_id, deviceId);

        if (rows.size() > 0) {
            std::string db_hash_token = rows[0][0].as<std::string>();

            if (!validateToken(refresh_token, db_hash_token)) {
                return {false, ""};
            }

            return {true, user_id};
        }
        else {
            return {false, ""};
        }
    }

    static bool validateToken (std::string token, std::string hashToken) {
        return bcrypt::validatePassword(token, hashToken);
    }
};
