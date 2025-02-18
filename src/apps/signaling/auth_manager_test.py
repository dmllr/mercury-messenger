from auth_manager import AuthManager

auth_manager = AuthManager()

# Issue token
token = ""
try:
    token = auth_manager.issue_token("user1", "password1")
    print(f"Issued token: {token}")
except ValueError as e:
    print(e)

# Validate token
token_data = auth_manager.validate_token(token)
if token_data:
    print("Token is valid:", token_data)
else:
    print("Invalid token")

token = ""
try:
    token = auth_manager.issue_token("user2", "password1")
    print(f"Issued token: {token}")
except ValueError as e:
    print(e)

# Validate token
token_data = auth_manager.validate_token(token)
if token_data:
    print("Token is valid:", token_data)
else:
    print("Invalid token")
