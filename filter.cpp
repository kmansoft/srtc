#include "srtc/filter.h"

namespace srtc
{

template <class T>
Filter<T>::Filter()
	: mValue(std::nullopt)
{
}

template <class T>
void Filter<T>::update(T value)
{
	if (mValue.has_value()) {
		mValue = static_cast<T>(mValue.value() * 0.9f + value * 0.1f);
	} else {
		mValue = value;
	}
}

template <class T>
T Filter<T>::get() const
{
	if (mValue.has_value()) {
		return mValue.value();
	}
	return {};
}

template class Filter<float>;

} // namespace srtc
