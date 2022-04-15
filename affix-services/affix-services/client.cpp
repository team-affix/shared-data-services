#include "client.h"
#include "cryptopp/osrng.h"
#include "affix-base/vector_extensions.h"
#include "transmission_result.h"
#include "pending_connection.h"
#include "affix-base/string_extensions.h"

#if 1
#define LOG(x) std::clog << x << std::endl
#define LOG_ERROR(x) std::cerr << x << std::endl
#else
#define LOG(x)
#define LOG_ERROR(x)
#endif

using namespace affix_services;
using namespace asio::ip;
using std::vector;
using affix_base::data::ptr;
using affix_services::networking::authenticated_connection;
using std::lock_guard;
using std::mutex;
using affix_base::threading::cross_thread_mutex;
using affix_base::cryptography::rsa_key_pair;
using affix_base::cryptography::rsa_to_base64_string;
using affix_base::data::to_string;
using affix_services::networking::transmission_result;
using affix_services::networking::transmission_result_strings;
using affix_services::pending_connection;
using affix_services::connection_information;
using namespace affix_base::threading;
using namespace affix_base::data;

client::client(
	asio::io_context& a_io_context,
	affix_base::data::ptr<client_configuration> a_client_configuration,
	affix_base::data::ptr<agent_information> a_agent_information
) :
	m_io_context(a_io_context),
	m_client_configuration(a_client_configuration),
	m_agent_information(a_agent_information)
{
	// Get the local identity string from the local public key.
	if (!rsa_to_base64_string(a_client_configuration->m_local_key_pair.resource().public_key, m_local_identity))
		throw std::exception("The inputted base64 RSA public key string is not in correct format.");

	// Lock vector of registered clients
	locked_resource l_registered_clients = m_registered_clients.lock();
	// Create and register the only path from the local client to the local client
	client_information l_local_client_information(m_local_identity, *m_agent_information);
	l_local_client_information.register_path({ m_local_identity });
	// Register the local client in the index
	l_registered_clients->push_back(l_local_client_information);


	if (m_client_configuration->m_enable_server.resource())
		// If the server is enabled, start it
		start_server();

	// Begin connecting to the default remote parties
	start_pending_outbound_connections();
	
}

void client::process(

)
{
	process_pending_outbound_connections();
	process_connection_results();
	process_authentication_attempts();
	process_authentication_attempt_results();
	process_authenticated_connections();
	process_received_messages();
	process_relay_messages();
	process_client_path_messages();
	process_agent_information_messages();
	process_pending_function_calls();
	process_registered_clients();
}

void client::relay(
	const std::vector<std::string>& a_path,
	const std::vector<uint8_t>& a_payload
)
{
	// Lock the vector of relay requests, allowing for pushing back
	locked_resource l_relay_requests = m_relay_messages.lock();

	// Generate message body
	message_relay_body l_message_body = message_relay_body(m_local_identity, a_payload, a_path);

	// Generate full message
	message l_message(l_message_body.create_message_header(), l_message_body);

	// Add this message to the queue to process
	l_relay_requests->push_back(l_message);

}

void client::relay(
	const std::string& a_identity,
	const std::vector<uint8_t>& a_payload
)
{
	relay(fastest_path_to_identity(a_identity), a_payload);
}

void client::register_local_index(
	const affix_base::data::ptr<affix_services::networking::authenticated_connection>& a_authenticated_connection
)
{
	// Lock the vector of reveal requests allowing for pushing back
	locked_resource l_client_path_messages = m_client_path_messages.lock();

	// Lock the vector of registered clients.
	locked_resource l_registered_clients = m_registered_clients.lock();

	for (int i = 0; i < l_registered_clients->size(); i++)
	{
		for (int j = 0; j < l_registered_clients->at(i).m_paths.size(); j++)
		{
			// Generate the index message body
			message_client_path_body l_message_index_body(l_registered_clients->at(i).m_paths.at(j), true);

			// Construct the whole message
			message l_message(l_message_index_body.create_message_header(), l_message_index_body);

			// Send this single path to the neighbor
			async_send_message(a_authenticated_connection, l_message);

		}

		// Construct the message body for agent_information
		message_agent_information_body l_message_agent_information_body(l_registered_clients->at(i).m_identity, l_registered_clients->at(i).m_agent_information);

		// Construct the whole message
		message l_agent_information_message(l_message_agent_information_body.create_message_header(), l_message_agent_information_body);

		// Send the agent_information to the neighbor
		async_send_message(a_authenticated_connection, l_agent_information_message);

	}

}

void client::deregister_neighbor_index(
	const std::string& a_neighbor_identity
)
{
	// Lock the vector of reveal requests allowing for pushing back
	locked_resource l_client_path_messages = m_client_path_messages.lock();

	// Generate the index message body
	message_client_path_body l_message_index_body({a_neighbor_identity}, false);

	// Construct the whole message
	message l_message(l_message_index_body.create_message_header(), l_message_index_body);

	// Push the index request to the queue to process
	l_client_path_messages->push_back(l_message);

}

void client::disclose_agent_information(

)
{
	// Lock the vector of reveal requests allowing for pushing back
	locked_resource l_reveal_requests = m_agent_information_messages.lock();

	// Generate the index message body
	message_agent_information_body l_message_index_body({}, *m_agent_information);

	// Construct the whole message
	message l_message(l_message_index_body.create_message_header(), l_message_index_body);

	// Push the index request to the queue to process
	l_reveal_requests->push_back(l_message);

	// Increment the version number of the agent_information
	m_agent_information->m_version_number++;

}

std::vector<std::string> client::fastest_path_to_identity(
	const std::string& a_identity
)
{
	locked_resource l_registered_clients = m_registered_clients.lock();

	std::vector<std::string> l_result;

	for (auto i = l_registered_clients->begin();
		i != l_registered_clients->end();
		i++)
	{
		if (i->m_identity != a_identity)
			continue;
		return i->fastest_path();
	}

	return l_result;

}

void client::start_server(

)
{
	// Log a server bootup message to the standard output
	LOG("[ CONNECTION PROCESSOR ] Starting server.");

	// Create acceptor object using the specified endpoint
	m_acceptor = new tcp::acceptor(m_io_context, tcp::endpoint(tcp::v4(), m_client_configuration->m_server_bind_port.resource()));

	// Begin accepting connections
	async_accept_next();
}

void client::start_pending_outbound_connections(

)
{
	// Get remote endpoints to which we should connect
	std::vector<std::string> l_remote_endpoints = m_client_configuration->m_remote_endpoint_strings.resource();

	// Connect to remote parties
	for (int i = 0; i < l_remote_endpoints.size(); i++)
	{
		std::vector<std::string> l_remote_endpoint_string_split = affix_base::data::string_split(l_remote_endpoints[i], ':');

		// Check if the remote endpoint is localhost
		bool l_remote_localhost = l_remote_endpoint_string_split[0] == "localhost";

		asio::ip::tcp::endpoint l_remote_endpoint;

		// Configure address of remote endpoint
		if (!l_remote_localhost)
			l_remote_endpoint.address(asio::ip::make_address(l_remote_endpoint_string_split[0]));

		// Configure port of remote endpoint
		l_remote_endpoint.port(std::stoi(l_remote_endpoint_string_split[1]));

		// Start pending outbound connection
		LOG("[ APPLICATION ] Connecting to: " << l_remote_endpoints[i]);
		start_pending_outbound_connection(l_remote_endpoint, l_remote_localhost);

	}
}

void client::start_pending_outbound_connection(
	asio::ip::tcp::endpoint a_remote_endpoint,
	const bool& a_remote_localhost
)
{
	// Lock mutex preventing concurrent pushes/pops from pending outbound connections vector.
	affix_base::threading::locked_resource l_locked_resource = m_pending_outbound_connections.lock();

	
	// Instantiate local endpoint object.
	tcp::endpoint l_local_endpoint(tcp::v4(), 0);


	// If the remote is localhost, assign the remote endpoint's address to the local IP address
	if (a_remote_localhost)
	{
		asio::ip::address l_local_ip_address;

		// Get local IP address
		if (!affix_base::networking::socket_internal_ip_address(l_local_ip_address))
		{
			std::cerr << "Unable to get the local ip address." << std::endl;

			// If the local IP address is unable to be retrieved, set the outbound connection to retry
			restart_pending_outbound_connection(a_remote_endpoint, a_remote_localhost);

			return;

		}

		a_remote_endpoint = tcp::endpoint(l_local_ip_address, a_remote_endpoint.port());

	}


	// Create new pending connection and push it to the back of the vector.
	l_locked_resource->push_back(
		new pending_connection(
			new connection_information(
				new tcp::socket(m_io_context, l_local_endpoint),
				a_remote_endpoint,
				a_remote_localhost,
				l_local_endpoint,
				false,
				false
			),
			m_connection_results
		));

}

void client::restart_pending_outbound_connection(
	asio::ip::tcp::endpoint a_remote_endpoint,
	const bool& a_remote_localhost
)
{
	locked_resource l_pending_function_calls = m_pending_function_calls.lock();

	// The inclusive minimum UTC time at which this pending function should trigger.
	uint64_t l_time_to_reconnect = affix_base::timing::utc_time() + m_client_configuration->m_reconnect_delay_in_seconds.resource();

	// Create delayed function call
	l_pending_function_calls->push_back(
		std::tuple(
			l_time_to_reconnect,
			[&, a_remote_endpoint, a_remote_localhost]
			{
				// Start a normal pending connection.
				start_pending_outbound_connection(a_remote_endpoint, a_remote_localhost);
			}
		));
}

void client::async_receive_message(
	affix_base::data::ptr<affix_services::networking::authenticated_connection> a_authenticated_connection
)
{
	ptr<std::vector<uint8_t>> l_message_data = new std::vector<uint8_t>();

	// Begin receiving data asynchronously, the dynamically allocated vector above will be captured by the lambda for use after callback.
	a_authenticated_connection->async_receive(*l_message_data,
		[&, a_authenticated_connection, l_message_data]()
		{
			// Lock mutex preventing concurrent reads/writes to the vector of received messages
			locked_resource l_received_messages = m_received_messages.lock();

			// Push the raw bytes of the message to a queue to be processed
			l_received_messages->insert(
				l_received_messages->begin(),
				std::tuple(a_authenticated_connection, l_message_data)
			);

			// Begin receiving yet another message
			async_receive_message(a_authenticated_connection);

		});

}

std::vector<affix_base::data::ptr<authenticated_connection>>::iterator client::find_connection(
	std::vector<affix_base::data::ptr<affix_services::networking::authenticated_connection>>& a_authenticated_connections,
	const std::string& a_remote_identity
)
{
	return std::find_if(a_authenticated_connections.begin(), a_authenticated_connections.end(),
		[&](affix_base::data::ptr<authenticated_connection> a_authenticated_connection)
		{
			return a_authenticated_connection->remote_identity() == a_remote_identity;
		});
}

bool client::identity_approved(
	const CryptoPP::RSA::PublicKey& a_identity
)
{
	try
	{
		// Extract the identity of the remote peer
		std::string l_identity;
		if (!rsa_to_base64_string(a_identity, l_identity))
			return false;

		// Get current approved identities
		std::vector<std::string>& l_approved_identities = m_client_configuration->m_approved_identities.resource();

		return std::find(l_approved_identities.begin(), l_approved_identities.end(), l_identity) !=
			l_approved_identities.end();

	}
	catch (std::exception a_exception)
	{
		LOG_ERROR("[ CONNECTION PROCESSOR ] Error checking identity approval: " << a_exception.what());

		return false;

	}
}

void client::process_pending_outbound_connections(

)
{
	// Lock the mutex, preventing changes to m_unauthenticated_connections.
	locked_resource l_pending_outbound_connections = m_pending_outbound_connections.lock();

	// Decrement through vector, since processing will erase each element
	for (int i = l_pending_outbound_connections->size() - 1; i >= 0; i--)
		process_pending_outbound_connection(l_pending_outbound_connections.resource(), l_pending_outbound_connections->begin() + i);

}

void client::process_pending_outbound_connection(
	std::vector<affix_base::data::ptr<pending_connection>>& a_pending_outbound_connections,
	std::vector<affix_base::data::ptr<pending_connection>>::iterator a_pending_outbound_connection
)
{
	// Store local variable describing the finished/unfinished state of the pending outbound connection.
	bool l_finished = false;

	{
		// Lock the state mutex for the pending outbound connection object
		locked_resource l_locked_resource = (*a_pending_outbound_connection)->m_finished.lock();

		// Extract state from object
		l_finished = *l_locked_resource;
	}

	if (l_finished)
	{
		// Erase pending outbound connection
		a_pending_outbound_connections.erase(a_pending_outbound_connection);
	}

}

void client::process_connection_results(

)
{
	// Lock the mutex, preventing changes to m_unauthenticated_connections.
	locked_resource l_connection_results = m_connection_results.lock();

	// Decrement through vector, since processing will erase each element
	for (int i = l_connection_results->size() - 1; i >= 0; i--)
		process_connection_result(l_connection_results.resource(), l_connection_results->begin() + i);

}

void client::process_connection_result(
	std::vector<affix_base::data::ptr<connection_result>>& a_connection_results,
	std::vector<affix_base::data::ptr<connection_result>>::iterator a_connection_result
)
{
	if ((*a_connection_result)->m_successful)
	{
		// Lock mutex for authentication attempts
		locked_resource l_authentication_attempts = m_authentication_attempts.lock();

		// Buffer in which the remote seed lives
		std::vector<uint8_t> l_remote_seed(affix_services::security::AS_SEED_SIZE);

		// Generate remote seed
		CryptoPP::AutoSeededRandomPool l_random;
		l_random.GenerateBlock(l_remote_seed.data(), l_remote_seed.size());

		// Create authentication attempt
		ptr<pending_authentication> l_authentication_attempt(
			new pending_authentication(
				(*a_connection_result)->m_connection_information,
				l_remote_seed,
				m_client_configuration->m_local_key_pair.resource(),
				m_authentication_attempt_results,
				m_client_configuration->m_enable_pending_authentication_timeout.resource(),
				m_client_configuration->m_pending_authentication_timeout_in_seconds.resource()
			)
		);

		// Push new authentication attempt to back of vector
		l_authentication_attempts->push_back(l_authentication_attempt);
		
	}
	else if (!(*a_connection_result)->m_connection_information->m_inbound)
	{
		// Reconnect to the remote peer
		restart_pending_outbound_connection(
			(*a_connection_result)->m_connection_information->m_remote_endpoint,
			(*a_connection_result)->m_connection_information->m_remote_localhost
		);

	}

	// Remove unauthenticated connection from vector
	a_connection_results.erase(a_connection_result);

}

void client::process_authentication_attempts(

)
{
	// Lock mutex for authentication attempts
	locked_resource l_authentication_attempts = m_authentication_attempts.lock();

	// Decrement through vector, since each process call will erase the element
	for (int i = l_authentication_attempts->size() - 1; i >= 0; i--)
		process_authentication_attempt(l_authentication_attempts.resource(), l_authentication_attempts->begin() + i);

}

void client::process_authentication_attempt(
	std::vector<affix_base::data::ptr<pending_authentication>>& a_authentication_attempts,
	std::vector<affix_base::data::ptr<pending_authentication>>::iterator a_authentication_attempt
)
{
	// Local variable outside of unnamed scope
	// describing the finished state of the authentication attempt
	bool l_finished = false;

	// Should stay its own scope because of std::lock_guard
	{
		// Lock the authentication attempt's state mutex while we read it
		locked_resource l_locked_resource = (*a_authentication_attempt)->m_finished.lock();

		// Extract the finished state of the authentication attempt
		l_finished = l_locked_resource.resource();
		
	}
	
	// Utilize the extracted information
	if (l_finished)
	{
		// Just erase the authentication attempt
		a_authentication_attempts.erase(a_authentication_attempt);

	}

}

void client::process_authentication_attempt_results(

)
{
	// Lock mutex preventing concurrent reads/writes to m_authentication_attempt_results.
	locked_resource l_authentication_attempt_results = m_authentication_attempt_results.lock();

	// Decrement through vector, since each call to process will erase elements.
	for (int i = l_authentication_attempt_results->size() - 1; i >= 0; i--)
		process_authentication_attempt_result(l_authentication_attempt_results.resource(), l_authentication_attempt_results->begin() + i);

}

void client::process_authentication_attempt_result(
	std::vector<affix_base::data::ptr<authentication_result>>& a_authentication_attempt_results,
	std::vector<affix_base::data::ptr<authentication_result>>::iterator a_authentication_attempt_result
)
{
	if ((*a_authentication_attempt_result)->m_successful && 
		identity_approved((*a_authentication_attempt_result)->m_security_information->m_remote_public_key))
	{
		// Lock mutex for authenticated connections
		locked_resource l_authenticated_connections = m_authenticated_connections.lock();

		// Log the success of the authentication attempt
		LOG("============================================================");
		LOG("[ PROCESSOR ] Success: authentication attempt successful: " << std::endl);
		LOG("Remote IPv4: " << (*a_authentication_attempt_result)->m_connection_information->m_socket->remote_endpoint().address().to_string() << ":" << (*a_authentication_attempt_result)->m_connection_information->m_socket->remote_endpoint().port());
		LOG("Remote Identity (base64): " << std::endl << (*a_authentication_attempt_result)->m_security_information->m_remote_identity << std::endl);
		LOG("Remote Seed: " << to_string((*a_authentication_attempt_result)->m_security_information->m_remote_token.m_seed, "-"));
		LOG("Local Seed:  " << to_string((*a_authentication_attempt_result)->m_security_information->m_local_token.m_seed, "-"));
		LOG("============================================================");

		// Create authenticated connection object
		ptr<authenticated_connection> l_authenticated_connection(
			new authenticated_connection(
				(*a_authentication_attempt_result)->m_connection_information,
				(*a_authentication_attempt_result)->m_security_information
			)
		);

		// Push authenticated connection object onto vector
		l_authenticated_connections->push_back(l_authenticated_connection);

		// Sends our entire index to the neighbor
		register_local_index(l_authenticated_connection);

		// Begin receiving data from socket
		async_receive_message(l_authenticated_connection);
		
	}
	else
	{

		// Print error message
		if (!(*a_authentication_attempt_result)->m_successful)
		{
			// Log the success of the authentication attempt
			LOG("[ PROCESSOR ] Error: authentication attempt failed: ");
			LOG("Remote IPv4: " << (*a_authentication_attempt_result)->m_connection_information->m_socket->remote_endpoint().address().to_string() << ":" << (*a_authentication_attempt_result)->m_connection_information->m_socket->remote_endpoint().port());
		}
		else
		{
			// Log the success of the authentication attempt
			LOG("[ PROCESSOR ] Error: authentication attempt succeeded but identity not approved: ");
			LOG("Remote Identity (base64): " << std::endl << (*a_authentication_attempt_result)->m_security_information->m_remote_identity << std::endl);

		}

		// Close the socket. Authentication was bad or identity not approved
		(*a_authentication_attempt_result)->m_connection_information->m_socket->close();

		if (!(*a_authentication_attempt_result)->m_connection_information->m_inbound)
		{
			// If the connection was outbound, reconnect to the remote peer
			restart_pending_outbound_connection(
				(*a_authentication_attempt_result)->m_connection_information->m_remote_endpoint,
				(*a_authentication_attempt_result)->m_connection_information->m_remote_localhost
			);
		}

	}

	// Erase authentication attempt result object
	a_authentication_attempt_results.erase(a_authentication_attempt_result);
	
}

void client::process_authenticated_connections(

)
{
	// Lock mutex for authenticated connections
	locked_resource l_authenticated_connections = m_authenticated_connections.lock();

	// Decrement through vector since processing might erase elements from the vector.
	for (int i = l_authenticated_connections->size() - 1; i >= 0; i--)
		process_authenticated_connection(l_authenticated_connections.resource(), l_authenticated_connections->begin() + i);
}

void client::process_authenticated_connection(
	std::vector<affix_base::data::ptr<affix_services::networking::authenticated_connection>>& a_authenticated_connections,
	std::vector<affix_base::data::ptr<authenticated_connection>>::iterator a_authenticated_connection
)
{
	bool l_connection_timed_out = m_client_configuration->m_enable_authenticated_connection_timeout.resource() &&
		(*a_authenticated_connection)->idletime() >
		m_client_configuration->m_authenticated_connection_timeout_in_seconds.resource();

	// Boolean describing whether the authenticated connection is still active (connected)
	bool l_connected = false;

	// Boolean describing whether there are callbacks currently dispatched that have not yet been triggered
	bool l_callbacks_currently_dispatched = false;

	{
		// This must stay it's own scope
		locked_resource l_connection_connected = (*a_authenticated_connection)->m_connected.lock();

		l_connected = *l_connection_connected;

		// Get whether there are still send callbacks dispatched
		locked_resource l_send_dispatcher_dispatched_count = (*a_authenticated_connection)->m_send_dispatcher.dispatched_count();

		// Get whether there are still receive callbacks dispatched
		locked_resource l_receive_dispatcher_dispatched_count = (*a_authenticated_connection)->m_receive_dispatcher.dispatched_count();

		l_callbacks_currently_dispatched = ((*l_send_dispatcher_dispatched_count) > 0) || ((*l_receive_dispatcher_dispatched_count) > 0);

	}

	if (l_connection_timed_out && (l_connected || l_callbacks_currently_dispatched))
	{
		// Handle disposing of connection.

		// Close connection, since it has timed out (this should cause the connection's receive callback to trigger with a failure response).
		(*a_authenticated_connection)->close();
		
	}

	if (!l_connected && !l_callbacks_currently_dispatched)
	{
		if (!(*a_authenticated_connection)->m_connection_information->m_inbound)
		{
			LOG("[ CONNECTION PROCESSOR ] Reconnecting to remote peer...");

			// Reconnect to the remote peer
			restart_pending_outbound_connection(
				(*a_authenticated_connection)->m_connection_information->m_remote_endpoint,
				(*a_authenticated_connection)->m_connection_information->m_remote_localhost
			);
		}

		// Deregisters all paths that require the neighbor to be connected, and informs all other neighbors of the change.
		deregister_neighbor_index((*a_authenticated_connection)->remote_identity());

		// Since the connection is no longer be active, just erase the object.
		a_authenticated_connections.erase(a_authenticated_connection);

	}

}

void client::process_received_messages(

)
{
	// Lock the mutex preventing concurrent reads/writes to the vector
	locked_resource l_received_messages = m_received_messages.lock();

	for (int i = l_received_messages->size() - 1; i >= 0; i--)
		process_received_message(l_received_messages.resource(), l_received_messages->begin() + i);

}

void client::process_received_message(
	std::vector<std::tuple<affix_base::data::ptr<affix_services::networking::authenticated_connection>, affix_base::data::ptr<std::vector<uint8_t>>>>& a_received_messages,
	std::vector<std::tuple<affix_base::data::ptr<affix_services::networking::authenticated_connection>, affix_base::data::ptr<std::vector<uint8_t>>>>::iterator a_received_message
)
{
	// Get data out of tuple
	ptr<authenticated_connection> l_authenticated_connection =
		std::get<0>((*a_received_message));
	ptr<std::vector<uint8_t>> l_message_data =
		std::get<1>((*a_received_message));

	// FIRST THING: ERASE THIS ITERATOR FROM THE VECTOR
	a_received_messages.erase(a_received_message);

	// Create byte buffer from which all message-specific data will be unpacked
	affix_base::data::byte_buffer l_message_data_byte_buffer(*l_message_data);

	// Get message header out of byte buffer
	message_header<message_types, affix_base::details::semantic_version_number> l_message_header;
	if (!l_message_header.deserialize(l_message_data_byte_buffer))
	{
		LOG_ERROR("[ APPLICATION ] Error unpacking message header (or) body.");
		l_authenticated_connection->close();
		return;
	}

	switch (l_message_header.m_message_type)
	{
		case message_types::relay:
		{
			// Lock the mutex preventing concurrent reads/writes to the vector
			locked_resource l_relay_requests = m_relay_messages.lock();

			message_relay_body l_message_body;

			if (!l_message_body.deserialize(l_message_data_byte_buffer))
			{
				LOG_ERROR("[ APPLICATION ] Error deserializing a message_relay_body.");
				l_authenticated_connection->close();
				return;
			}

			// Push the received relay request onto the vector
			l_relay_requests->push_back(
					message { l_message_header, l_message_body });

			break;
		}
		case message_types::client_path:
		{
			// Lock the mutex preventing concurrent reads/writes to the vector
			locked_resource l_client_path_messages = m_client_path_messages.lock();

			message_client_path_body l_message_body;

			if (!l_message_body.deserialize(l_message_data_byte_buffer))
			{
				LOG_ERROR("[ APPLICATION ] Error deserializing a message_client_path_body.");
				l_authenticated_connection->close();
				return;
			}

			// Push the received relay request onto the vector
			l_client_path_messages->push_back(
				message{ l_message_header, l_message_body });

			break;
		}
		case message_types::agent_information:
		{
			// Lock the mutex preventing concurrent reads/writes to the vector
			locked_resource l_reveal_requests = m_agent_information_messages.lock();

			message_agent_information_body l_message_body;

			if (!l_message_body.deserialize(l_message_data_byte_buffer))
			{
				LOG_ERROR("[ APPLICATION ] Error deserializing the a message_agent_information_body.");
				l_authenticated_connection->close();
				return;
			}

			// Push the received relay request onto the vector
			l_reveal_requests->push_back(
					message{ l_message_header, l_message_body });

			break;
		}
		default:
		{
			LOG_ERROR("[ APPLICATION ] Error: message_type was invalid.");
			l_authenticated_connection->close();
			return;
		}
	}

}

void client::process_relay_messages(

)
{
	// Lock the mutex preventing new relay requests from being received
	locked_resource l_relay_requests = m_relay_messages.lock();

	// Decrement through vector since elements of the vector might be removed
	for (int i = l_relay_requests->size() - 1; i >= 0; i--)
		process_relay_message(l_relay_requests.resource(), l_relay_requests->begin() + i);

}

void client::process_relay_message(
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_relay_body>>& a_relay_messages,
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_relay_body>>::iterator a_relay_message
)
{
	// Lock the mutex preventing concurrent reads/writes to the authenticated connections vector
	locked_resource l_authenticated_connections = m_authenticated_connections.lock();

	// Get the request out from the std::tuple
	message<message_types, affix_base::details::semantic_version_number, message_relay_body> l_request = *a_relay_message;

	// Erase the request from the vector
	a_relay_messages.erase(a_relay_message);

	if (l_request.m_message_body.m_path.front() != m_local_identity)
		// Something went wrong; the local identity does not match the identity that should have received this request
		return;

	// Erase the local identity from the path
	l_request.m_message_body.m_path.erase(l_request.m_message_body.m_path.begin());

	if (l_request.m_message_body.m_path.size() == 0)
	{
		// This module is the intended recipient

		// Lock the mutex of received relays
		locked_resource l_agent_received_messages = m_agent_received_messages.lock();

		// Add the payload
		l_agent_received_messages->push_back(l_request);

	}

	// Get the recipient's identity from the request
	std::string l_recipient_identity = l_request.m_message_body.m_path.front();

	std::vector<ptr<authenticated_connection>>::iterator l_recipient_connection = std::find_if(l_authenticated_connections->begin(), l_authenticated_connections->end(),
			[&](ptr<authenticated_connection> a_recipient_authenticated_connection)
			{
				return a_recipient_authenticated_connection->remote_identity() == l_recipient_identity;
			});

	if (l_recipient_connection == l_authenticated_connections->end())
		// The recipient is not connected
		return;

	// Set the version in the message header
	l_request.m_message_header.m_version = affix_services::i_affix_services_version;

	async_send_message(*l_recipient_connection, l_request);

}

void client::process_client_path_messages(

)
{
	locked_resource l_client_path_messages = m_client_path_messages.lock();

	for (int i = l_client_path_messages->size() - 1; i >= 0; i--)
		process_client_path_message(l_client_path_messages.resource(), l_client_path_messages->begin() + i);

}

void client::process_client_path_message(
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_client_path_body>>& a_client_path_messages,
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_client_path_body>>::iterator a_client_path_message
)
{
	// Extract useful data from iterator
	message l_message = *a_client_path_message;

	// Erase iterator from vector
	a_client_path_messages.erase(a_client_path_message);

	// Detail the version of the message header as our local version.
	l_message.m_message_header.m_version = i_affix_services_version;

	// Prefix the client path with our local identity.
	l_message.m_message_body.m_client_path.insert(l_message.m_message_body.m_client_path.begin(), m_local_identity);

	// Lock the vector of registered clients.
	locked_resource l_registered_clients = m_registered_clients.lock();

	std::vector<client_information>::iterator l_client_information = std::find_if(l_registered_clients->begin(), l_registered_clients->end(),
		[&](const client_information& a_client_information)
		{
			return a_client_information.m_identity == l_message.m_message_body.m_client_path.back();
		});

	if (l_client_information == l_registered_clients->end())
	{
		// If the client information is not currently registered, create a mostly empty client information to populate.
		l_client_information = l_registered_clients->insert(l_registered_clients->end(),
			client_information(l_message.m_message_body.m_client_path.back(), agent_information()));
	}

	if (l_message.m_message_body.m_register)
	{
		// We are registering a path
		if (!l_client_information->register_path(l_message.m_message_body.m_client_path))
			// If the path is already registered, stop the recursion here. (Don't relay to neighbors)
			return;

	}
	else
	{
		// We are deregistering a path. This path must be deregistered from ALL client_information objects, 
		// since some paths to any client may be dependent on the path we are deregistering.
		for (int i = l_registered_clients->size() - 1; i >= 0; i--)
		{
			l_registered_clients->at(i).deregister_paths_starting_with(l_message.m_message_body.m_client_path);
		}

	}

	// Lock vector of authenticated connections
	locked_resource l_authenticated_connections = m_authenticated_connections.lock();

	for (int i = 0; i < l_authenticated_connections->size(); i++)
	{
		if (std::find(l_message.m_message_body.m_client_path.begin(),
			l_message.m_message_body.m_client_path.end(),
			l_authenticated_connections->at(i)->remote_identity()) !=
			l_message.m_message_body.m_client_path.end())
			// Don't send a reveal request to this peer. They are already a part of the path.
			continue;

		// Send the message to the remote client
		async_send_message(l_authenticated_connections->at(i), l_message);

	}


}

void client::process_agent_information_messages(

)
{
	// Lock the mutex preventing concurrent reads/writes to the index_requests vector
	locked_resource l_reveal_requests = m_agent_information_messages.lock();

	for (int i = l_reveal_requests->size() - 1; i >= 0; i--)
		process_agent_information_message(l_reveal_requests.resource(), l_reveal_requests->begin() + i);

}

void client::process_agent_information_message(
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_agent_information_body>>& a_agent_information_messages,
	std::vector<message<message_types, affix_base::details::semantic_version_number, message_agent_information_body>>::iterator a_agent_information_message
)
{
	// Extract data out from iterator
	message l_request = *a_agent_information_message;

	// Erase the iterator before doing work with the data retrieved from it
	a_agent_information_messages.erase(a_agent_information_message);

	// Set the message's version to this client's version (since we will be redistributing the message to our neighbors)
	l_request.m_message_header.m_version = i_affix_services_version;

	// Lock the vector of known relay paths
	locked_resource l_registered_clients = m_registered_clients.lock();

	// Try to find an entry for the client
	std::vector<client_information>::iterator l_registered_client =
		std::find_if(l_registered_clients->begin(), l_registered_clients->end(),
			[&](client_information& a_client_information)
			{
				return a_client_information.m_identity == l_request.m_message_body.m_client_identity;
			});

	if (l_registered_client != l_registered_clients->end())
	{
		if (l_request.m_message_body.m_agent_information.newer_than(l_registered_client->m_agent_information))
		{
			// Update the agent information of the registered client
			l_registered_client->m_agent_information = l_request.m_message_body.m_agent_information;
		}
		else
		{
			// Do nothing, and DO NOT redistribute this agent_information to the remote peers.
			return;
		}
	}
	// else (DO NOTHING)

	// Get the current authenticated connections
	locked_resource l_authenticated_connections = m_authenticated_connections.lock();

	for (int i = 0; i < l_authenticated_connections->size(); i++)
	{
		// Send the message to the remote client
		async_send_message(l_authenticated_connections->at(i), l_request);

	}

}

void client::process_pending_function_calls(

)
{
	// Lock the vector preventing concurrent reads/writes to it
	locked_resource l_pending_function_calls = m_pending_function_calls.lock();

	// Process each individual pending function call request
	for (int i = l_pending_function_calls->size() - 1; i >= 0; i--)
		process_pending_function_call(l_pending_function_calls.resource(), l_pending_function_calls->begin() + i);

}

void client::process_pending_function_call(
	std::vector<std::tuple<uint64_t, std::function<void()>>>& a_pending_function_calls,
	std::vector<std::tuple<uint64_t, std::function<void()>>>::iterator a_pending_function_call
)
{
	// Get the time when the pending function call was created
	const uint64_t& l_call_time = std::get<0>(*a_pending_function_call);

	if (affix_base::timing::utc_time() >= l_call_time)
	{
		// Extract the actual function from the function call request.
		std::function<void()> l_function = std::get<1>(*a_pending_function_call);

		// Erase the pending function call from the vector BEFORE CALLING the function.
		// We want to be the ones to invalidate this iterator by erasing it, instead of have the possibility
		// where the function we call writes to the vector while we're using the iterator.
		a_pending_function_calls.erase(a_pending_function_call);
		
		// Call the function associated with this pending function call request
		l_function();

	}

}

void client::async_accept_next(

)
{
	m_acceptor->async_accept(
		[&](asio::error_code a_ec, tcp::socket a_socket)
		{
			// Store the new socket in the list of connections
			locked_resource l_connection_results = m_connection_results.lock();

			try
			{
				// Extract remote endpoint from socket object
				asio::ip::tcp::endpoint l_remote_endpoint = a_socket.remote_endpoint();

				// Extract local endpoint from socket object
				asio::ip::tcp::endpoint l_local_endpoint = a_socket.local_endpoint();

				// Initialize connection information struct
				affix_base::data::ptr<connection_information> l_connection_information = new connection_information(
					new tcp::socket(std::move(a_socket)),
					l_remote_endpoint,
					false,
					l_local_endpoint,
					true,
					true
				);

				l_connection_results->push_back(
					new connection_result(
						l_connection_information,
						!a_ec
					)
				);

			}
			catch (std::exception a_exception)
			{
				LOG_ERROR("[ SERVER ] Error: " << a_exception.what());
				return;
			}

			// If there was an error, return and do not make another async 
			// accept request. Otherwise, try to accept another connection.
			if (!a_ec)
				async_accept_next();

		});
}


void client::process_registered_clients(

)
{
	locked_resource l_registered_clients = m_registered_clients.lock();

	for (int i = l_registered_clients->size() - 1; i >= 0; i--)
		process_registered_client(l_registered_clients.resource(), l_registered_clients->begin() + i);

}

void client::process_registered_client(
	std::vector<client_information>& a_registered_clients,
	std::vector<client_information>::iterator a_registered_client
)
{
	if (a_registered_client->m_paths.size() == 0)
		// If there are no remaining valid paths, erase the client registration.
		a_registered_clients.erase(a_registered_client);

}
