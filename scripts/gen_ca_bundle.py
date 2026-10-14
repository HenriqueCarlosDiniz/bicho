"""
Gera include/ca_roots.h com as CAs raiz usadas pelas APIs do Bicho (Gemini, Open-Meteo,
AwesomeAPI, GitHub Status, BrasilAPI, HackerNews, Scryfall, PokeAPI).

As raízes são extraídas do bundle Mozilla distribuído pelo pacote Python 'certifi':
    python scripts/gen_ca_bundle.py

Para conferir qual raiz cada host usa:
    openssl s_client -connect HOST:443 -servername HOST -showcerts </dev/null
"""
import os
import re
import sys

import certifi

ROOT_LABELS = [
    "GTS Root R1",                 # googleapis.com, firebaseio.com (raiz autoassinada; o mbedTLS
                                   # aceita a cadeia cruzada pela GlobalSign por casar sujeito+chave)
    "GTS Root R3",
    "GTS Root R4",                 # Cloudflare (awesomeapi, brasilapi, scryfall, pokeapi)
    "ISRG Root X1",                # Let's Encrypt (open-meteo, githubstatus)
    "ISRG Root X2",
    "DigiCert Global Root G2",     # Folga para trocas de CDN
    "Amazon Root CA 1",
    "USERTrust RSA Certification Authority",
]


def main():
    bundle = open(certifi.where(), encoding="utf-8").read()
    blocks = re.findall(r"# Label: \"([^\"]+)\".*?(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)",
                        bundle, re.S)
    by_label = {label: pem for label, pem in blocks}

    missing = [label for label in ROOT_LABELS if label not in by_label]
    if missing:
        sys.exit("CAs ausentes no certifi: %s" % ", ".join(missing))

    out_path = os.path.join(os.path.dirname(__file__), "..", "include", "ca_roots.h")
    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("#ifndef CA_ROOTS_H\n#define CA_ROOTS_H\n\n")
        out.write("// =============================================================================\n")
        out.write("// CAs raiz confiáveis para as conexões HTTPS do ESP32-S3 (gerado por scripts/gen_ca_bundle.py)\n")
        out.write("// Não edite à mão: rode o script novamente para atualizar.\n")
        out.write("// =============================================================================\n\n")
        out.write("constexpr const char* TRUSTED_ROOT_CA_BUNDLE =\n")
        for label in ROOT_LABELS:
            out.write("    // %s\n" % label)
            for line in by_label[label].strip().splitlines():
                out.write("    \"%s\\n\"\n" % line.strip())
        out.write("    ;\n\n#endif // CA_ROOTS_H\n")
    print("Gerado %s com %d CAs raiz." % (os.path.normpath(out_path), len(ROOT_LABELS)))


if __name__ == "__main__":
    main()
