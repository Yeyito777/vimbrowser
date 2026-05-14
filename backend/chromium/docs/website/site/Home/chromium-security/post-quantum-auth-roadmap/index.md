---
breadcrumbs:
- - /Home
  - Chromium
- - /Home/chromium-security
  - Chromium Security
- - /Home/chromium-security/post-quantum-auth-roadmap
  - Post-Quantum HTTPS Authentication Roadmap
page_name: post-quantum-auth-roadmap
title: Post-Quantum HTTPS Authentication Roadmap
---

_David Benjamin and Joe DeBlasio, 2026-02-27_

This document outlines a roadmap for post-quantum HTTPS server authentication in Chromium, that is, how a TLS connection is authenticated by an HTTPS server’s TLS key, which is in turn associated with the HTTPS server identity by a certificate from a trusted certification authority (CA).

Transitioning to a PQ web will be a long process with many moving parts, and we expect details to evolve over time. As such, this document is not a precise specification but a rough outline that aims to help reason about security and compatibility properties. We divide the transition into four stages:

* [Stage 1: Add Post-Quantum Options.](#stage-1-add-post-quantum-options) Making PQ auth options available on the client without removing any classical options. This starts the transition but does not not give security benefits.
* [Stage 2: Opt-in Individual Sites.](#stage-2-opt-in-individual-sites) An HSTS-like mechanism to require PQ on sites that opt in, as a stopgap measure until Stage 3.
* [Stage 3: Require a Post-Quantum PKI.](#stage-3-require-a-post-quantum-pki) Requiring PQ CAs (but not necessarily PQ TLS keys) from all sites for robust downgrade protection.
* [Stage 4: Require Post-Quantum TLS Keys.](#stage-4-require-post-quantum-tls-keys) Completing the transition in a **far-future** stage that requires all sites to have fully migrated to PQ.

## Stage 1: Add Post-Quantum Options

Chromium is already working to add support for PQ authentication in TLS. This includes PQ-secure CAs with [Merkle Tree Certificates](https://security.googleblog.com/2026/02/cultivating-robust-and-efficient.html) and [ML-DSA TLS keys](https://datatracker.ietf.org/doc/draft-ietf-tls-mldsa/).

However, without removing classical options, this does not protect connections from a cryptographically relevant quantum computer (CRQC). An attacker can always target the client’s weakest supported authentication option. Even if, say, `https://example.com` were to adopt post-quantum authentication, a CRQC can still compromise the key of *any* trusted classical CA, forge a certificate, and impersonate the site. This attack is possible no matter what CA `https://example.com` actually uses.

At the end of the transition, when Chromium has removed *all* classical options, this attack will not be possible. However, such a Chromium would be incompatible with all existing classical servers, so this is only viable when the *entire* Web is PQ-capable. That will take a long time.

Thus we need intermediate stages that offer *downgrade protection*: protecting PQ-capable sites that need protection against a CRQC, even while supporting sites that remain on classical keys.

## Stage 2: Opt-in Individual Sites

Requiring a post-quantum PKI (see Stage 3 below) achieves downgrade protection but may still take some time due to the changes required to the CA ecosystem. If necessary, we can provide downgrade protection even sooner with an analog to [HTTP Strict Transport Security (HSTS)](https://www.rfc-editor.org/rfc/rfc6797.html).

HSTS defines a `Strict-Transport-Security` HTTP header which allows HTTPS servers to opt into only being accessed over HTTPS. This works with a combination of client state and a [preload list](https://hstspreload.org/). We could deploy an analogous HTTP Require-Post-Quantum (HRPQ) response header, for example:

```
Require-Post-Quantum: max-age=31536000; includeSubDomains
```

When connecting to a Require-Post-Quantum origin, Chromium would not allow classical CAs or classical TLS keys. This would allow sites to opt-in to stronger behavior. This could be deployed in a stateful form (updated in response to HTTP headers), a preload list, or a combination of the two.

As an opt-in mechanism, HRPQ is attractive for compatibility. However, there are risks and complications to both forms of this mechanism:

* A stateful mechanism cannot protect the first connection to a site. To protect the first connection, we must use a preload list.

* A preload list grows as more sites migrate to PQ. When an appreciable fraction of all sites on the Web are PQ-capable, it will become too large.

* A stateful mechanism is a deployment risk for site operators. If a site enables HRPQ and discovers an incompatibility, clients may be unable to discover the rollback and reconnect until `max-age` expires. However, a short `max-age` makes the mechanism ineffective. Stateful failures are also harder to diagnose, as different clients may be in different states.

* A preload list is a deployment risk for site operators. If a site is on some HRPQ preload list, but discovers an incompatibility, they must wait weeks or more for a [browser update](https://hstspreload.org/removal/) before their site is fixed.

* Protecting a single domain is insufficient due to cookie scoping. PQ-insecure origins can still inject domain-scoped cookies into PQ-secure origins. Domain-scoped cookies from PQ-secure origins will still be leaked to PQ-insecure origins. While one can mitigate this by *only* reading and writing cookies with the `__Host-` [cookie prefix](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Set-Cookie#cookie_prefixes), this requires sites to opt in. Like HSTS, HRPQ needs `includeSubDomains` to be secure by default.

* `includeSubDomains` is a deployment risk for operators. Setting it on `example.com` will break *any* subdomain that is not yet PQ-capable.

* `includeSubDomains` means this must be a separate header from `Strict-Transport-Security`. A site may be ready for `includeSubDomains` with HSTS, but not with HRPQ, or vice versa.

* A stateful mechanism gives more avenues for cross-site correlation of user activity, but applying [network state partitioning](https://chromestatus.com/feature/6713488334389248) will lower the effectiveness of HRPQ.

* Even if a site is PQ-capable, clients may be configured to trust a TLS interception proxy (e.g. in enterprise) that is not yet PQ-capable. Clients that are sometimes behind the proxy and sometimes not may pick up the header, then break when they move to the proxy again. This can be mitigated by only enforcing on known roots, but this further lowers the effectiveness of HRPQ.

* Both stateful and preload mechanisms are difficult to deploy in non-browser clients. Non-browser clients may maintain less state, or may be unable to regularly update a preload list.

As more sites become PQ-capable, these risks will only increase. As a result, HRPQ is not a good long-term answer for downgrade protections. Requiring a post-quantum PKI will allow the removal of HRPQ support.

## Stage 3: Require a Post-Quantum PKI

There are far fewer CA operators than server operators on the Web. Servers already have to regularly obtain new certificates from their CAs, and TLS server certificate provisioning is increasingly automated with protocols like ACME. Designed right, transitions within just the PKI can complete much quicker.

Suppose we target a Chromium that *only accepts PQ CA signatures*. That is, a Chromium that:

* Trusts PQ CAs (e.g. with [Merkle Tree Certificates](https://datatracker.ietf.org/doc/draft-ietf-plants-merkle-tree-certs/))
* Does *not* trust any classical CAs
* Allows PQ TLS keys
* Still allows classical TLS keys (in certificates signed by PQ CAs)

These clients can both statelessly provide downgrade protection *and* can reach this state without waiting for every site to become PQ-capable. Sites would transition as follows:

* Today, servers only have classical credentials and are incompatible with PQ-CA-only clients.

* A server can, without changing any of its own key material, transition to a pair of certificates: a classical-to-classical certificate for legacy clients and a PQ-to-classical certificate for PQ clients.

* Once a server has provisioned a PQ key, it can transition to a fully PQ-capable state: a classical-to-classical credential for legacy clients, and a PQ-to-PQ credential for PQ clients.

<img src="/Home/chromium-security/post-quantum-auth-roadmap/pq-ca-only-transition.png" alt="A diagram of the transition described above, using arrows to describe certificates. The first step is a server with only a certificate from a classical CA covering a classical TLS key. This can automatically transition to the second step: one certificate from a classical CA to a classical TLS key and one certificate from a PQ CA to the same classical TLS key. This can manually transition to the third step: one certificate from a classical CA to a classical TLS key and one certificate from a PQ CA to a PQ TLS key. The first two steps are labeled &quot;Servers without a PQ key&quot;. The third step is labeled &quot;Servers with a PQ key&quot;. The second and third steps are labeled &quot;Compatible with a PQ-CA-only client&quot;. There is a legend explaining that certificates (arrows) from PQ CAs are for PQ clients, and the certificates (arrows) from classical CAs are for legacy clients.">

While a server can transition directly to the final state (many likely will), it must provision new keys and update their serving software to do so. We cannot hope to automate this at the PKI alone.

However, the intermediate state *can* be automated. Server software does not need PQ support to serve a PQ certificate—the signature from the CA is broadly opaque to the TLS serving stack. A CA can be updated to issue certificates that are PQ-signed using the server’s existing TLS key, paired with a certificate signed by a traditional CA (how to support this pairing without requiring server-side changes is described below). Clients can then authenticate the most secure signature that they recognize, while ignoring the alternate option.

Once a client has removed all classical CAs, its connections to these PQ-capable servers will be secure against a CRQC. Crucially, the only accepted certificates are those from PQ CAs. An attacker cannot forge a certificate that tricks a client into accepting a classical TLS key for a PQ-capable server.

<img src="/Home/chromium-security/post-quantum-auth-roadmap/downgrade-protection.png" alt="A diagram showing how this provides compatibility and downgrade protection by comparing the view from classical and PQ-CA-only clients. The top half of the diagram, labeled &quot;What a classical client sees&quot;, shows how, in both servers with and without PQ keys, only the classical-CA-to-classical-TLS-key certificates exist to classical clients. The bottom half of the diagram, labeled &quot;What a PQ-CA-only client sees&quot;, shows how, only the PQ-CA-issued certificates exist to PQ-CA-only clients. In the server without a PQ key, this client sees the PQ-CA-to-classical-TLS-key certificate. In a server with a PQ key, this client only sees the PQ-CA-to-PQ-TLS-key certificate. The classical-CA-to-classical-TLS-key certificate, and thus the classical TLS key, is invisible.">

For servers maintaining both a classical and PQ key, downgrade protection is contingent on the servers ensuring classical TLS keys are not compatible with a PQ-CA-only client. That is *if* the server has a PQ TLS key, it should *only* ask the PQ CA to sign its PQ key, not its classical key. Otherwise an attacker can compromise authentication via the classical key. These misconfigurations will be visible via transparency logs.

<img src="/Home/chromium-security/post-quantum-auth-roadmap/misconfiguration.png" alt="A diagram showing the above misconfiguration. There are three arrows (certificates): classical CA to classical TLS key, PQ CA to classical TLS key, and PQ CA to PQ TLS key. The classical CA and the first arrow are grayed out, to indicate the view from a PQ-CA-only client. However, both arrows from the PQ CA, and thus the classical TLS key, are visible. This shows that, in this misconfiguration, the PQ-CA-only client accepts and is thus vulnerable to the classical TLS key.">

Later sections will discuss how to design this transition so that it can be automated.

### Certificate Negotiation

Throughout this transition, servers will need to retain compatibility with legacy clients. Legacy and PQ clients can not trust the same CAs, so every PQ transition unavoidably requires sending different certificates to different clients. TLS solves these kinds of problems with [certificate negotiation](https://www.rfc-editor.org/rfc/rfc8446#section-4.4.2.2):

* The `signature_algorithms` extension determines whether the server should use a classical or PQ TLS key
* [Trust anchor negotiation](https://github.com/tlswg/tls-trust-anchor-ids), e.g. with `certificate_authorities` or `trust_anchors` extensions, determines whether the server should use a certificate from one CA or another


Together, these allow server software to correctly and automatically select the right certificates with the right clients in each of the above configurations.

To provision these certificates in the PQ-incapable case, ACME extensions like [ACME Profile Sets](https://github.com/davidben/acme-profile-sets) can help CAs transparently provision two certificates for the same key. ACME extensions cannot automatically provision the PQ-capable case, but this is unavoidable: to become PQ-capable, a server must first provision a new private key, and update to server software that can load it.

However, some of these capabilities are fairly new, and we need to automatically update servers that do not yet support them. Thus, while certificate negotiation and profile sets will help for the *next* authentication transition, the automated transition for servers to support classical/PQ-paired certificates will need an additional tool:

### Alternate Certificate Issuer

Many existing PQ-incompatible servers, and their ACME pipelines, are only capable of configuring *one* certificate. We can do this by embedding the post-quantum certificate inside the classical one. Define an X.509 extension, `id-alternateCertificateIssuer` that describes the issuer-specific differences between the classical certificate and the equivalent post-quantum certificate. Specifically:

* Replacement serial number
* Replacement issuer name
* Issuer-specific extensions to add, remove, and replace, such as AIA, AKID, and embedded SCTs
* Replacement signature algorithm
* Replacement signature

A PQ client that recognizes this extension will reconstruct the PQ-CA-to-classical-key certificate and verify it, ignoring the certificate's classical signature. A legacy client will ignore the extension and see an otherwise normal purely classical certificate. The TLS server software will likewise ignore the new extension.

Crucially, this extension *only* overrides issuer-specific information, not subject information. This is to avoid a situation where the TLS server software inspects the certificate to derive some of its own configuration (e.g. configuring SNI dispatch from subjectAltNames), but then a PQ client uses different information altogether.

<img src="/Home/chromium-security/post-quantum-auth-roadmap/embedded-certificate.png" alt="A diagram showing two certificates embedded in each other. There are two boxes representing CAs. The box for the PQ CA is inside the box for the classical CA. There are two parallel arrows (certificates) from these boxes to a classical TLS key. One arrow starts from the outer classical CA box. The other starts from the inner PQ CA box. There is a legend indicating that the classical CA box is for legacy clients, and the PQ CA box is for PQ clients.">

*This* transition can be performed automatically, as the TLS server software will simply see a new X.509 extension, which it will ignore.

From the CA side, the issuance process will be:

1. Issue the PQ-signature-to-classical-key certificate.
2. Extract the PQ-issuer-specific differences into an `id-alternateCertificateIssuer` extension.
3. Issue a classical-to-classical certificate with this extension embedded.

We’ll need to take some care in how we define this extension. X.509 is extensible and does not clearly differentiate subject and issuer fields. We should ensure:

* It is not possible to override the public key, as the server software may expect to find the public key in the existing field.
* It is not possible to override key usage and extended key usage, as server software may expect to find it in the existing extensions.
* It is not possible to override subject alt names, as server software may expect to find it in the existing extensions.

This extension is undesirable for the long-term:

- It is more complex.
- The combined certificate is larger than either certificate on its own.
- While compatible with [standalone](https://davidben.github.io/merkle-tree-certs/draft-ietf-plants-merkle-tree-certs.html#name-standalone-certificates) Merkle Tree Certificates, limiting to one certificate is incompatible with [landmark certificates](https://davidben.github.io/merkle-tree-certs/draft-ietf-plants-merkle-tree-certs.html#name-landmark-certificates) optimization.

We intend for this to be a purely transitionary mechanism, based on the limitations of existing, older servers. For newer servers, the more principled certificate negotiation approach solves this more efficiently and flexibly.

To enforce this, clients will only implement `id-alternateCertificateIssuer` when the `subjectPublicKeyInfo` field is classical. PQ-capable servers have two TLS keys and necessarily can already manage distinct certificate chains, so the extension is unnecessary. This also means that when a PQ-CA-only client migrates to fully PQ-only (Stage 4 below), support for the extension can be removed.

### Removing HRPQ

A PQ-CA-only client does not need HRPQ for downgrade protection. Once a client purely supports PQ CAs, it can remove HRPQ, avoiding the associated risks and complications. Clients that were unable to deploy HRPQ, such as more constrained non-browser clients, can instead skip to this stage once the HTTPS server ecosystem is ready.

## Stage 4: Require Post-Quantum TLS Keys

Finally, to ensure *all* HTTPS connections are PQ-secure, clients will eventually transition to a PQ-only state. This means:

* Removing support for classical CAs (Stage 3 above)
* Removing support for classical TLS keys
* Removing support for classical TLS key agreement
* Removing support for TLS 1.2 and below, which cannot support post-quantum

**This is a far-future stage**, and requires all servers to upgrade to a fully PQ-capable configuration to remain compatible with PQ-only clients. To help the client eventually reach this state, we can apply some combination of the following tools to aid sites in transitioning:

* Warnings in DevTools and developer outreach
* Non-blocking UI treatment, such as indicators in the address bar
* Blocking UI treatment, such as interstitial warnings
* Temporary administrative policies for enterprise deployments that need more time

As the actual client change at this stage requires the Web ecosystem be fully ready, we expect this to take a very, very long time to complete and provide security benefits. The earlier stages outlined in this document are thus important to achieve post-quantum security before then.
