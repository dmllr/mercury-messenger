import os

def test_compilation(compile_binaries):
    """
    This test acts as a gatekeeper for the integration test suite.
    Due to alphabetical ordering (test_00_build.py), pytest will execute it first.
    It triggers the session-scoped 'compile_binaries' fixture.
    If compilation fails, pytest.exit() is called in the fixture to immediately abort
    the session.
    """
    assert os.path.isdir(compile_binaries), f"Binary directory not found: {compile_binaries}"
    assert os.path.isfile(os.path.join(compile_binaries, "mercury-server")), "mercury-server binary is missing."
    assert os.path.isfile(os.path.join(compile_binaries, "mercury-mock-app")), "mercury-mock-app binary is missing."
