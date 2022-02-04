#pragma once
#include "affix-base/pch.h"
#include "rolling_token.h"
#include "asio.hpp"
#include "cryptopp/rsa.h"
#include "transmission_security_manager.h"
#include "affix-base/ts_deque.h"
#include "affix-base/ptr.h"
#include "affix-base/networking.h"
#include "affix-base/transmission.h"
#include "message_header.h"
#include "connection_async_receive_result.h"
#include "affix-base/cross_thread_mutex.h"
#include "affix-base/threading.h"
#include "connection_information.h"

namespace affix_services {
	namespace networking {

		class authenticated_connection
		{
		public:
			/// <summary>
			/// Holds relevant information about the connection.
			/// </summary>
			affix_base::data::ptr<connection_information> m_connection_information;

			/// <summary>
			/// Security manager, in charge of all security when sending / receiving data.
			/// </summary>
			affix_services::security::transmission_security_manager m_transmission_security_manager;

			/// <summary>
			/// Boolean describing if the connection is still valid.
			/// </summary>
			affix_base::threading::guarded_resource<bool, affix_base::threading::cross_thread_mutex> m_connected = true;

		protected:
			/// <summary>
			/// IO guard preventing concurrent reads/writes to the socket.
			/// </summary>
			affix_base::networking::socket_io_guard m_socket_io_guard;

			/// <summary>
			/// Time of creation of the authenticated_connection object.
			/// </summary>
			uint64_t m_start_time = 0;

			/// <summary>
			/// Time of last interaction between either of the parties.
			/// </summary>
			affix_base::threading::guarded_resource<uint64_t, affix_base::threading::cross_thread_mutex> m_last_interaction_time = 0;
			
			/// <summary>
			/// Vector of async_receive results.
			/// </summary>
			affix_base::threading::guarded_resource<std::vector<affix_base::data::ptr<connection_async_receive_result>>, affix_base::threading::cross_thread_mutex>& m_receive_results;

		public:
			/// <summary>
			/// Destructor, handles deletion of resources.
			/// </summary>
			virtual ~authenticated_connection(

			);

			/// <summary>
			/// Constructor taking all necessary information.
			/// </summary>
			/// <param name="a_socket"></param>
			/// <param name="a_local_private_key"></param>
			/// <param name="a_local_token"></param>
			/// <param name="a_remote_public_key"></param>
			/// <param name="a_remote_token"></param>
			/// <param name="a_receive_results_mutex"></param>
			/// <param name="a_receive_results"></param>
			authenticated_connection(
				affix_base::data::ptr<connection_information> a_connection_information,
				affix_base::data::ptr<security_information> a_security_information,
				affix_base::threading::guarded_resource<std::vector<affix_base::data::ptr<connection_async_receive_result>>, affix_base::threading::cross_thread_mutex>& a_receive_results
			);

		public:
			void async_send(
				affix_base::data::byte_buffer& a_byte_buffer,
				const std::function<void(bool)>& a_callback
			);
			void async_receive(
			
			);

		public:
			uint64_t lifetime() const;

		};

	}
}
