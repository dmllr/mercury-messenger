import time
from abc import ABC
from authlib.oauth2.rfc6749 import grants
from authlib.oauth2.rfc6750 import BearerTokenValidator
from authlib.common.security import generate_token


class TokenStore(ABC):
    def generate_token(self, client, grant_type, user, scope):
        ...

    def validate_token(self, token):
        ...


class InMemoryToken(TokenStore):
    def __init__(self):
        self.tokens = {}

    def generate_token(self, client, grant_type, user, scope):
        token = generate_token()
        self.tokens[token] = {
            'client_id': client.client_id if client else None,
            'user': user,
            'scope': scope,
            'expires_at': time.time() + 3600  # 1 hour expiry
        }
        return token

    def validate_token(self, token):
        token_data = self.tokens.get(token)
        if token_data and token_data['expires_at'] > time.time():
            return token_data
        return None


class PasswordGrant(grants.ResourceOwnerPasswordCredentialsGrant):
    def authenticate_user(self, username, password):
        if users.get(username) == password:
            return username
        return None


class CustomBearerTokenValidator(BearerTokenValidator):
    def __init__(self, token_store: TokenStore, **extra_attributes):
        super().__init__(**extra_attributes)
        self.__token_store = token_store;

    def authenticate_token(self, token_string):
        return self.__token_store.validate_token(token_string)

    def request_invalid(self, request):
        return False

    def token_revoked(self, token):
        return False


class AuthManager:

    def __init__(self):
        self.__token_store = InMemoryToken()
        self.__token_validator = CustomBearerTokenValidator(self.__token_store)
        self.__users = {
            "user1": "password1",
            "user2": "password2",
            "user3": "password3"
        }

    def authorize(self, username: str, password: str):
        return username in self.__users and self.__users[username] == password

    def generate_token(self, client, grant_type, user, scope):
        return self.__token_store.generate_token(client, grant_type, user, scope)

    def authenticate_token(self, token):
        return self.__token_validator.authenticate_token(token)

    def validate_token(self, token):
        return self.__token_store.validate_token(token)
