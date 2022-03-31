#pragma once

#define ARRAY_SIZE(a) \
  ((sizeof(a) / sizeof(*(a))) / \
	static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
