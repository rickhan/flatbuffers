/*
 * note  : 对flatbuffers的扩展，以方便使用
 * author: jianwu.han jianwu.han@7road.com
 * date  : 2017/06/16
 *
 */

#ifndef __FLATBUFFERS__EXT__H__
#define __FLATBUFFERS__EXT__H__

#include <type_traits>
#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <flatbuffers/flatbuffers.h>

namespace flatbuffers
{

	template<int FieldId, typename T>
	struct Field
	{
		typedef T value_type;
		const static int pos = FieldId;

		Field& operator=(const value_type value)
		{
			_value = value;
			return *this;
		}

		value_type _value;
	};

	template<int FieldId, typename T>
	struct Field<FieldId, T*>
	{
		typedef T* value_type;
		const static int pos = FieldId;

		Field& operator=(const value_type value)
		{
			_value = value;
			return *this;
		}

		value_type operator()()
		{
			return _value;
		}

		value_type _value;

	};

	template<int FieldId>
	struct Field<FieldId, std::string>
	{
		typedef std::string value_type;
		const static int pos = FieldId;

		Field& operator=(const value_type& value)
		{
			_value = value;
		}

		Field& operator=(const char* szValue)
		{
			_value = szValue;
			return *this;
		}

		value_type _value;
	};

	template<int FieldId, typename T>
	struct Field<FieldId, std::vector<T>>
	{
		typedef std::vector<T> value_type;
		const static int pos = FieldId;

		typename value_type::iterator begin()
		{
			return _value.begin();
		}

		typename value_type::iterator end()
		{
			return _value.end();
		}

		const T& operator[](std::size_t index)
		{
			return _value[index];
		}

		void push_back(const T& value)
		{
			_value.push_back(value);
		}

		value_type _value;
	};

	template<typename... Args> struct SerializeObj;
	template<int FieldId, typename... Args>
	struct Field <FieldId, SerializeObj<Args...>>
	{
		typedef SerializeObj<Args...> value_type;
		const static int pos = FieldId;

		value_type _value;
	};

	template<typename T>
	struct is_flatbuffer_field
	{
		const static bool value = false;
	};

	template<int FieldId, typename T>
	struct is_flatbuffer_field<Field<FieldId, T>>
	{
		const static bool value = true;
	};

	template<typename T>
	struct is_flatbuffer_struct
	{
		const static bool value = is_flatbuffer_field<T>::value && std::is_class<typename T::value_type>::value;
	};

	template<typename T>
	struct is_flatbuffer_vector
	{
		const static bool value = false;
	};

	template<int FieldId, typename T>
	struct is_flatbuffer_vector<Field<FieldId, std::vector<T>> >
	{
		const static bool value = true;
	};

	template<typename T>
	struct is_flatbuffer_string
	{
		const static bool value = false;
	};

	template<int FieldId>
	struct is_flatbuffer_string<Field<FieldId, std::string>>
	{
		const static bool value = true;
	};

	template<typename T>
	struct is_flatbuffer_native_table
	{
		const static bool vlaue = is_flatbuffer_field<T>::value && std::is_base_of<flatbuffers::NativeTable, typename T::value_type>::value;
	};

	template<typename U>
	struct SerializeStruct
	{
		static uoffset_t write(FlatBufferBuilder& builder, const U& value)
		{
			return builder.AddStruct<typename U::value_type>(&value._value);
		}
	};

	template<typename U>
	struct SerializeVector
	{
		static uoffset_t  write(FlatBufferBuilder& builder, const U& value)
		{
			auto offset = builder.CreateVector(value._value);
			return builder.AddOffset(offset);
		}
	};

	template<typename U>
	struct SerializeString
	{
		static uoffset_t  write(FlatBufferBuilder& builder, const U& value)
		{
			auto offset = builder.CreateString(value._value);
			return builder.AddOffset(offset);
		}
	};

	template<typename U>
	struct SerializeNormalField
	{
		static uoffset_t  write(FlatBufferBuilder& builder, const U& value)
		{
			return builder.AddElement<typename U::value_type>(value._value);
		}
	};

	struct table_finish{};
	table_finish& end_object()
	{
		static table_finish finish;
		return finish;
	}

	struct all_finish{};
	all_finish& end_all()
	{
		static all_finish finish;
		return finish;
	}

	template<typename T>
	struct SerializeObject
	{
		SerializeObject(FlatBufferBuilder& builder) : _builder(builder)
		{
			_start = _builder.StartTable();
			_end = 0;
		}

		inline void operator<<(const table_finish&)
		{
			if (!ended())finish();
		}

		inline void operator<<(const all_finish&)
		{
			if (!ended())finish();
			_builder.Finish(_end);
		}

		template<typename U, typename = std::enable_if<is_flatbuffer_field<U>::value, U>::type>
		inline SerializeObject& operator<<(const U& value)
		{
			if (hasWriten<U>()) return *this; // duplicate write are not allowed!

			typedef std::conditional<is_flatbuffer_vector<U>::value, SerializeVector<U>,
				std::conditional<is_flatbuffer_string<U>::value, SerializeString<U>,
				std::conditional<is_flatbuffer_struct<U>::value, SerializeStruct<U>, SerializeNormalField<U>>::type>::type>::type Write;

			auto off = Write::write(_builder, value);
			if (off != 0)
			{
				_offsetbuf[U::pos] = off;
			}

			return *this;
		}

		// serialize nested object
		template<typename U>
		inline SerializeObject& operator<<(SerializeObject<U>& obj)
		{
			if (obj.ended() == false) return *this; // 没序列化完的，不允许序列化
			auto offset = _builder.AddOffset(obj.end());
			if (offset == 0) return *this;
			_offsetbuf[U::pos] = offset;

			return *this;
		}

		template<typename U>
		inline SerializeObject<U> createChild()
		{
			return SerializeObject<U>(_builder);
		}

		template<typename U>
		bool hasWriten()
		{
			auto it = _offsetbuf.find(U::pos);
			if (it != _offsetbuf.end() && it->second != 0)
			{
				return true;
			}

			return false;
		}

		bool ended() { return !_end.IsNull(); }
		Offset<void> end() { return _end; }
	private:
		void finish(){ _end = _builder.EndTable(_start, _offsetbuf); }

	private:
		FlatBufferBuilder& _builder;
		uoffset_t _start;
		Offset<void> _end;
		std::unordered_map<voffset_t, uoffset_t> _offsetbuf;
	};

	class FlatBuffersStream
	{
	private:
		FlatBufferBuilder _builder;

	public:
		FlatBuffersStream() {}
		FlatBufferBuilder& builder() { return _builder; }

		template<typename T>
		SerializeObject<T> createSerializeObj()
		{
			return SerializeObject<T>(_builder);
		}
	};

	namespace detail
	{
		template <class T, std::size_t N, class... Args>
		struct get_number_of_element_from_tuple_by_type_impl
		{
			static /*constexpr*/ const std::size_t value = N;
		};

		template <class T, std::size_t N, class... Args>
		struct get_number_of_element_from_tuple_by_type_impl<T, N, T, Args...>
		{
			static /*constexpr*/const std::size_t value = N;
		};

		template <class T, std::size_t N, class U, class... Args>
		struct get_number_of_element_from_tuple_by_type_impl<T, N, U, Args...>
		{
			static /*constexpr*/const std::size_t value = get_number_of_element_from_tuple_by_type_impl<T, N + 1, Args...>::value;
		};
	}

	template <class T, class... Args>
	T& get_element_by_type(std::tuple<Args...>& t)
	{
		return std::get<detail::get_number_of_element_from_tuple_by_type_impl<T, 0, Args...>::value>(t);
	}

	template<typename... Args>
	struct SerializeObj : NativeTable
	{
		template<typename T>
		T& get()
		{
			return get_element_by_type<T>(fields);
		}

		template<typename T>
		void set(const typename T::value_type& value)
		{
			auto& v = get<T>();
			v._value = value;
		}

		std::tuple<Args...> fields;
	};
}
#endif // __FLATBUFFERS__EXT__H__