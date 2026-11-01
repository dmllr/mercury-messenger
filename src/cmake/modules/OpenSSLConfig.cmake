if(NOT TARGET eillance-ssl OR NOT TARGET eillance-crypto)
  message(FATAL_ERROR "OpenSSL targets not found. Make sure OpenSSL is built before find_package(OpenSSL)")
endif()

# Create OpenSSL targets
if(NOT TARGET OpenSSL::Crypto)
  add_library(OpenSSL::Crypto INTERFACE IMPORTED)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES eillance-crypto)
endif()

if(NOT TARGET OpenSSL::SSL)
  add_library(OpenSSL::SSL INTERFACE IMPORTED)
  set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_LINK_LIBRARIES "eillance-ssl;OpenSSL::Crypto")
endif()

# Standard variables
set(OPENSSL_FOUND TRUE)
set(OPENSSL_VERSION "1.1.1")

# Get include directories from OpenSSL targets
get_target_property(OPENSSL_INCLUDE_DIR eillance-crypto INTERFACE_INCLUDE_DIRECTORIES)

# If include dir is not set, try to set it manually
if(NOT OPENSSL_INCLUDE_DIR)
  # Try to get the source directory of OpenSSL
  get_target_property(openssl_source_dir eillance-crypto SOURCE_DIR)
  if(openssl_source_dir)
    set(OPENSSL_INCLUDE_DIR "${openssl_source_dir}/include")
  else()
    # Try to use the imported path
    set(OPENSSL_INCLUDE_DIR "${boringssl_path}/include")
    if(NOT EXISTS "${OPENSSL_INCLUDE_DIR}")
      message(WARNING "Cannot find OpenSSL include directory '${OPENSSL_INCLUDE_DIR}'. Set OPENSSL_INCLUDE_DIR manually.")
    endif()
  endif()
endif()

# Add include directories to the imported targets so try_compile checks work correctly
if(TARGET OpenSSL::Crypto)
  set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()
if(TARGET OpenSSL::SSL)
  set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()

set(OPENSSL_CRYPTO_LIBRARY OpenSSL::Crypto)
set(OPENSSL_SSL_LIBRARY OpenSSL::SSL)
set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})

mark_as_advanced(OPENSSL_INCLUDE_DIR)
