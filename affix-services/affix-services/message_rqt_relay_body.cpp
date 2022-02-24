#include "message_rqt_relay_body.h"

using namespace affix_services;
using namespace affix_base::cryptography;

message_rqt_relay_body::message_rqt_relay_body(

)
{

}

message_rqt_relay_body::message_rqt_relay_body(
	const std::vector<std::string>& a_path,
	const size_t& a_path_index,
	const std::vector<uint8_t>& a_payload
) :
	m_path(a_path),
	m_path_index(a_path_index),
	m_payload(a_payload)
{

}

message_header message_rqt_relay_body::create_message_header(

) const
{
	return message_header(message_types::rqt_relay, message_header::random_discourse_identifier());
}

bool message_rqt_relay_body::serialize(
	affix_base::data::byte_buffer& a_output,
	serialization_status_response_type& a_result
) const
{
	// Push the vector of exported identities onto the byte buffer
	if (!a_output.push_back(m_path))
	{
		a_result = serialization_status_response_type::error_packing_path;
		return false;
	}

	// Push the index of the current identity in the path onto the byte buffer
	if (!a_output.push_back(m_path_index))
	{
		a_result = serialization_status_response_type::error_packing_path_index;
		return false;
	}
	
	// Push the payload onto the byte buffer
	if (!a_output.push_back(m_payload))
	{
		a_result = serialization_status_response_type::error_packing_payload;
		return false;
	}

	return true;

}

bool message_rqt_relay_body::deserialize(
	affix_base::data::byte_buffer& a_input,
	deserialization_status_response_type& a_result
)
{
	// Pop the exported identities from the byte buffer.
	if (!a_input.pop_front(m_path))
	{
		a_result = deserialization_status_response_type::error_unpacking_path;
		return false;
	}

	// Pop the path index from the byte buffer
	if (!a_input.pop_front(m_path_index))
	{
		a_result = deserialization_status_response_type::error_unpacking_path_index;
		return false;
	}

	// Pop payload from byte buffer
	if (!a_input.pop_front(m_payload))
	{
		a_result = deserialization_status_response_type::error_unpacking_payload;
		return false;
	}

	return true;

}