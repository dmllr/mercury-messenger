import time
from abc import ABC, abstractmethod

from authlib.oauth2.rfc6750 import BearerTokenValidator
from authlib.common.security import generate_token


# Abstract TokenStore class
class TokenStore(ABC):
    @abstractmethod
    def generate_token(self, grant_type, user, scope):
        pass

    @abstractmethod
    def validate_token(self, token):
        pass


# In-memory token store implementation
class InMemoryToken(TokenStore):
    def __init__(self):
        self.tokens = {}

    def generate_token(self, grant_type, user, scope):
        token = generate_token()
        self.tokens[token] = {
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


# Custom Bearer token validator
class CustomBearerTokenValidator(BearerTokenValidator):
    def __init__(self, token_store: TokenStore, **extra_attributes):
        super().__init__(**extra_attributes)
        self.token_store = token_store

    def authenticate_token(self, token_string):
        return self.token_store.validate_token(token_string)

    def request_invalid(self, request):
        return False

    def token_revoked(self, token):
        return False


# Authentication manager
class AuthManager:
    def __init__(self):
        self.users = {
            "user1": "password1",
            "user2": "password2",
        }
        self.token_store = InMemoryToken()
        self.token_validator = CustomBearerTokenValidator(self.token_store)

    def authorize(self, username: str, password: str):
        return self.users.get(username) == password;

    def generate_token(self, grant_type, user, scope):
        return self.token_store.generate_token(grant_type, user, scope)

    def authenticate_token(self, token):
        return self.token_validator.authenticate_token(token)

    def validate_token(self, token):
        return self.token_store.validate_token(token)

    def issue_token(self, username: str, password: str):
        if self.authorize(username, password):
            return self.generate_token('password', username, 'read write')
        else:
            raise ValueError("Invalid credentials")
