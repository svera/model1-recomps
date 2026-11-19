#pragma once
#include <cstdint>

class model1;

struct local_link_status
{
	unsigned id = 0, participants = 0;
	uint64_t exchanges = 0;
};

// The session owns the link until destruction; all machines outlive the session.
class local_cabinet_link
{
public:
	virtual ~local_cabinet_link() = default;
	virtual void run_frame() = 0;
	virtual local_link_status status(unsigned cabinet) const = 0;
};
