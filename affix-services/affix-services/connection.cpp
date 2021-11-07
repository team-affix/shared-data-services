#include "connection.h"
#include "affix-base/pch.h"
#include "affix-base/utc_time.h"
#include "affix-base/transmission.h"
#include "affix-base/rsa.h"
#include "affix-base/catch_friendly_assert.h"

using namespace affix_services::networking;
using affix_base::timing::utc_time;
using asio::ip::tcp;
using namespace affix_base::networking;
using namespace affix_base::cryptography;

connection::connection(tcp::socket& a_socket) : m_socket(std::move(a_socket)), m_socket_io_guard(m_socket), m_start_time(utc_time()) {

}

void connection::async_send(const vector<uint8_t>& a_data, const RSA::PrivateKey& a_private_key, const function<void(bool)>& a_callback) {

	if (!secured()) {
		m_socket_io_guard.async_send(a_data, a_callback);
	}
	else {
		vector<uint8_t> l_encrypted = rsa_encrypt_in_chunks(a_data, m_outbound_public_key);
		m_socket_io_guard.async_send(l_encrypted, a_callback);
	}

}

void connection::async_receive(vector<uint8_t>& a_data, const RSA::PrivateKey& a_private_key, const function<void(bool)>& a_callback) {

	if (!secured()) {
		m_socket_io_guard.async_receive(a_data, a_callback);
	}
	else {
		m_socket_io_guard.async_receive(a_data, [&, a_private_key, a_callback] (bool a_result) {
			if (a_result)
				a_data = rsa_decrypt_in_chunks(a_data, a_private_key);
			a_callback(a_result);
		});
	}

}

bool connection::secured() const {
	AutoSeededRandomPool l_random;
	return m_outbound_public_key.Validate(l_random, 3)
		&& m_outbound_token.initialized()
		&& m_inbound_token.initialized();
}

uint64_t connection::lifetime() const {
	return utc_time() - m_start_time;
}
