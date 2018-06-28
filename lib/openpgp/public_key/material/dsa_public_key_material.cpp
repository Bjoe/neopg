// OpenPGP DSA public key material (implementation)
// Copyright 2018 The NeoPG developers
//
// NeoPG is released under the Simplified BSD License (see license.txt)

#include <neopg/dsa_public_key_material.h>

#include <neopg/intern/cplusplus.h>
#include <neopg/intern/pegtl.h>

using namespace NeoPG;

std::unique_ptr<DsaPublicKeyMaterial> DsaPublicKeyMaterial::create_or_throw(
    ParserInput& in) {
  auto data = make_unique<DsaPublicKeyMaterial>();
  data->m_p.parse(in);
  data->m_q.parse(in);
  data->m_g.parse(in);
  data->m_y.parse(in);
  return data;
}

void DsaPublicKeyMaterial::write(std::ostream& out) const {
  m_p.write(out);
  m_q.write(out);
  m_g.write(out);
  m_y.write(out);
}
