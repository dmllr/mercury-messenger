        for (int i = 0; i < 2; i++) {
            HCERTSTORE store;
            const CERT_CONTEXT *cert;
            const CRL_CONTEXT *crl;

            if (i == 0)
                store = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER , L"ROOT");
            else
                store = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"CA");

            if (store == NULL)
                throw 1;

            mbedtls_x509_crt mCaCert;
            mbedtls_ssl_conf_authmode(&mConf, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_x509_crt_init(&mCaCert);

            cert = CertEnumCertificatesInStore(store, NULL);
            while (cert != NULL) {
                if (cert->dwCertEncodingType == X509_ASN_ENCODING) {
                    std::string cacert = std::string(reinterpret_cast<const char *const>(cert->pbCertEncoded), cert->cbCertEncoded);

                    auto ret = mbedtls_x509_crt_parse(
                            &mCaCert, reinterpret_cast<const unsigned char *>(cacert.c_str()),
                            cacert.size());
                    if (ret == 0) {
                        mbedtls_ssl_conf_ca_chain(&mConf, &mCaCert, NULL);
                    }
                }
                cert = CertEnumCertificatesInStore(store, cert);
            }

//            crl = CertEnumCRLsInStore(store, NULL);
//            while (crl != NULL) {
//                if (crl->dwCertEncodingType == X509_ASN_ENCODING) {
//                    std::string cacert = std::string(reinterpret_cast<const char *const>(crl->pbCrlEncoded), crl->cbCrlEncoded);
//
//                    auto ret = mbedtls_x509_crt_parse(
//                            &mCaCert, reinterpret_cast<const unsigned char *>(cacert.c_str()),
//                            cacert.size());
//                    if (ret == 0) {
//                        mbedtls_ssl_conf_ca_chain(&mConf, &mCaCert, NULL);
//                    }
//                }
//                crl = CertEnumCRLsInStore(store, crl);
//            }

            CertCloseStore(store, 0);
        }
