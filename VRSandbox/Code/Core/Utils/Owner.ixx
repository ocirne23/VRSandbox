export module Utils.Owner;

import <type_traits>;

export namespace gsl
{
	template <class T, class = std::enable_if_t<std::is_pointer<T>::value>>
	using owner = T;
}