#pragma once

#include <optional>

namespace srtc
{

template <class T>
class Filter
{
public:
	Filter();

	void update(T value);
	T get() const;

private:
	std::optional<T> mValue;
};

}