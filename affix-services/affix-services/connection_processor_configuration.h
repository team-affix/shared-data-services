#pragma once
#include "affix-base/pch.h"
#include "affix-base/rsa.h"
#include "affix-base/cache.h"
#include "affix-base/ptr.h"
#include "json.hpp"
#include "asio.hpp"

namespace affix_services
{
	struct connection_processor_configuration : affix_base::data::cache<nlohmann::ordered_json>
	{
	public:
		/// <summary>
		/// Path to the JSON file.
		/// </summary>
		std::string m_json_file_path;

		/// <summary>
		/// Boolean describing whether or not timing out is enabled for pending authentication attempts.
		/// </summary>
		affix_base::data::cache<bool> m_enable_pending_authentication_timeout;

		/// <summary>
		/// Maximum time after which pending authentication attempts will be discarded.
		/// </summary>
		affix_base::data::cache<uint64_t> m_pending_authentication_timeout_in_seconds;

		/// <summary>
		/// Boolean describing whether or not to close sockets after the connections have idled for a maximum amount of time.
		/// </summary>
		affix_base::data::cache<bool> m_enable_authenticated_connection_timeout;

		/// <summary>
		/// Maximum amount of time that connections can idle for before they must be closed and reestablished.
		/// </summary>
		affix_base::data::cache<uint64_t> m_authenticated_connection_timeout_in_seconds;

		/// <summary>
		/// The local RSA key pair, used for all message security
		/// </summary>
		affix_base::data::cache<affix_base::cryptography::rsa_key_pair> m_local_key_pair;

		/// <summary>
		/// The delay in seconds for which the connection processor should wait before reconnecting to a remote peer.
		/// </summary>
		affix_base::data::cache<uint64_t> m_reconnect_delay_in_seconds;

		/// <summary>
		/// Import all identities approved for using this module.
		/// </summary>
		affix_base::data::cache<std::vector<std::string>> m_approved_identities;

		/// <summary>
		/// Remote endpoints which this module will connect to.
		/// </summary>
		affix_base::data::cache<std::vector<asio::ip::tcp::endpoint>> m_remote_endpoints;

	public:
		/// <summary>
		/// Constructor which takes an argument for each field it is to populate.
		/// </summary>
		/// <param name="a_connection_enable_disconnect_after_maximum_idle_time"></param>
		/// <param name="a_connection_maximum_idle_time_in_seconds"></param>
		connection_processor_configuration(
			const std::string& a_json_file_path
		);

	};
}

