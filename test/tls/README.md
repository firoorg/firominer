These public test credentials are only for the loopback TLS tests. Never use
the private key outside these tests or install the CA in a machine trust store.
The server certificate is signed by the test CA, valid until 2126, and covers
only the DNS name `localhost`; `127.0.0.1` deliberately fails hostname checks.
