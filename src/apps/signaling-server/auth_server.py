from authlib.integrations.flask_oauth2 import AuthorizationServer, ResourceProtector, current_token
from authlib.oauth2.rfc6749 import grants
from authlib.oauth2.rfc6749.tokens import BearerToken
from authlib.oauth2.rfc6750 import BearerTokenValidator
from authlib.common.security import generate_token
from flask import Flask, request, jsonify
import time

app = Flask(__name__)

users = {
    "user1": "password1",
    "user2": "password2",
    "user3": "password3"
}

class InMemoryToken:
    def __init__(self):
        self.tokens = {}

    def generate_token(self, client, grant_type, user, scope):
        token = generate_token()
        self.tokens[token] = {
            'client_id': client.client_id,
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

token_store = InMemoryToken()

class PasswordGrant(grants.ResourceOwnerPasswordCredentialsGrant):
    def authenticate_user(self, username, password):
        if users.get(username) == password:
            return username
        return None

class CustomBearerTokenValidator(BearerTokenValidator):
    def authenticate_token(self, token_string):
        return token_store.validate_token(token_string)

    def request_invalid(self, request):
        return False

    def token_revoked(self, token):
        return False

authorization = AuthorizationServer(
    token_generator=BearerToken(InMemoryToken.generate_token)
)
authorization.register_grant(PasswordGrant)
require_oauth = ResourceProtector()
require_oauth.register_token_validator(CustomBearerTokenValidator())

@app.route('/oauth/token', methods=['POST'])
def issue_token():
    return authorization.create_token_response()

if __name__ == '__main__':
    authorization.init_app(app)
    app.run(port=5000)
