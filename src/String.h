#pragma once

#include <string>
#include <algorithm>
#include <sstream>
#include <charconv>
#include <filesystem>
#include <vector>

#if defined(__cpp_lib_char8_t)
#define __U8stringUseChar8
#endif

namespace String
{
	constexpr int Version[]{ 1, 0, 0, 0 };

	namespace __Detail
	{
		template<class, class = void> struct HasValueType : std::false_type {};
		template<class T> struct HasValueType<T, std::void_t<typename T::value_type>> : std::true_type {};

		template<class T, typename ToMatch>
		struct IsCString
		: std::integral_constant<
			bool,
			std::is_same<ToMatch const *, typename std::decay<T>::type>::value ||
			std::is_same<ToMatch *, typename std::decay<T>::type>::value
		> {};

		template<typename T>
		std::filesystem::path ToStringImpl(const T& t)
		{
            if constexpr (std::is_same_v<T, std::filesystem::path>)
            {
                return t;
            }
			else if constexpr (std::is_same_v<T, std::string::value_type>
				|| std::is_same_v<T, std::wstring::value_type>
				|| std::is_same_v<T, decltype(std::filesystem::path{}.u8string())::value_type>
				|| std::is_same_v<T, std::u16string::value_type>
				|| std::is_same_v<T, std::u32string::value_type>)
			{
				return std::basic_string<T>(1, t);
			}
			else if constexpr (std::is_integral_v<T>)
			{
				constexpr auto bufSiz = 65;
				char buf[bufSiz]{0};
				if (const auto [p, e] = std::to_chars(buf, buf + bufSiz, t); e != std::errc{}) throw std::runtime_error("ToStringImpl error: invalid literal: " + std::string(p));
				return buf;
			}
			else if constexpr (std::is_floating_point_v<T>)
			{
				std::ostringstream buf;
				buf << t;
				return buf.str();
			}
			else if constexpr (IsCString<T, char>::value
				|| IsCString<T, wchar_t>::value
				|| IsCString<T, decltype(std::filesystem::path{}.u8string())::value_type>::value
				|| IsCString<T, char16_t>::value
				|| IsCString<T, char32_t>::value)
			{
				return t;
			}
			else if constexpr (HasValueType<T>::value)
			{
				if constexpr ((std::is_base_of_v<std::basic_string<typename T::value_type>, T>
					|| std::is_base_of_v<std::basic_string_view<typename T::value_type>, T>))
				{
					return t;
				}
				else
				{
					std::u32string str = U"{";
					auto i = t.begin();
					auto end = t.end();
					if (end - i == 0) return U"{}";
					for (; i < end - 1; ++i)
					{
						str.append(ToStringImpl(*i).u32string());
						str.append(U", ");
					}
					str.append(ToStringImpl(*i).u32string());
					str.append(U"}");
					return str;
				}
			}
			else
			{
				std::ostringstream buf;
				buf << t;
				return buf.str();
			}
		}

		template<typename T> struct GetStrFunc{};
		template<> struct GetStrFunc<std::string> { decltype(auto) operator()(const std::filesystem::path& str) const { return str.string(); } };
		template<> struct GetStrFunc<std::wstring> { decltype(auto) operator()(const std::filesystem::path& str) const { return str.wstring(); } };
#ifdef __U8stringUseChar8
		template<> struct GetStrFunc<std::u8string> { decltype(auto) operator()(const std::filesystem::path& str) const { return str.u8string(); } };
#endif
		template<> struct GetStrFunc<std::u16string> { decltype(auto) operator()(const std::filesystem::path& str) const { return str.u16string(); } };
		template<> struct GetStrFunc<std::u32string> { decltype(auto) operator()(const std::filesystem::path& str) const { return str.u32string(); } };

#ifdef __U8stringUseChar8
#define __Suit2(macro, func)\
	macro(std::string, func)\
	macro(std::wstring, func##W)\
    macro(std::u8string, func##U8)\
	macro(std::u16string, func##U16)\
	macro(std::u32string, func##U32)

#define __Suit3(macro, src, func)\
	macro(src, std::string, func)\
	macro(src, std::wstring, func##W)\
    macro(src, std::u8string, func##U8)\
	macro(src, std::u16string, func##U16)\
	macro(src, std::u32string, func##U32)
#else
#define __Suit2(macro, func)\
	macro(std::string, func)\
	macro(std::wstring, func##W)\
	macro(std::u16string, func##U16)\
	macro(std::u32string, func##U32)

#define __Suit3(macro, src, func)\
	macro(src, std::string, func)\
	macro(src, std::wstring, func##W)\
	macro(src, std::u16string, func##U16)\
	macro(src, std::u32string, func##U32)
#endif

#define __Args1(src, type, func) template<typename Str> type func(const Str& str) { return src<type>(str); }
	}

#pragma region UpperLower
	template<typename Str>
	void Upper(Str& string)
	{
		std::transform(string.begin(), string.end(), string.begin(), static_cast<int(*)(int)>(std::toupper));
	}

	template<typename Ret, typename Str>
	Ret ToUpperAs(const Str& str)
	{
		auto buf = str;
		Upper(buf);
		if constexpr (std::is_same_v<Ret, Str>) return buf;
		return __Detail::GetStrFunc<Ret>{}(buf);
	}
	
	__Suit3(__Args1, ToUpperAs, ToUpper)
	
	template<typename Str>
	void Lower(Str& string)
	{
		std::transform(string.begin(), string.end(), string.begin(), static_cast<int(*)(int)>(std::tolower));
	}

	template<typename Ret, typename Str>
	Ret ToLowerAs(const Str& str)
	{
		auto buf = str;
		Lower(buf);
		if constexpr (std::is_same_v<Ret, Str>) return buf;
		return __Detail::GetStrFunc<Ret>{}(buf);
	}

	__Suit3(__Args1, ToLowerAs, ToLower)
#pragma endregion UpperLower

#pragma region Pad
#define __PadImpl(src, type, funcName)\
	template<typename Str>\
	type funcName(const Str& str, const std::uint32_t width, const typename Str::value_type pad)\
		{ return src<type>(str, width, pad); }
	
	template<typename Str>
	void PadLeftTo(Str& str, const std::uint32_t width, const typename Str::value_type pad)
	{
		std::int64_t n = width - str.length();
		if (n <= 0) return;
		str.insert(str.begin(), n, pad);
	}

	template<typename Ret, typename Str>
	Ret PadLeftAs(const Str& str, const std::uint32_t width, const typename Str::value_type pad)
	{
		auto buf = str;
		PadLeftTo(buf, width, pad);
		if constexpr (std::is_same_v<Ret, Str>) return buf;
		return __Detail::GetStrFunc<Ret>{}(buf);
	}
	
	__Suit3(__PadImpl, PadLeftAs, PadLeft)

	template<typename Str>
	void PadRightTo(Str& str, const std::uint32_t width, const typename Str::value_type pad)
	{
		std::int64_t n = width - str.length();
		if (n <= 0) return;
		str.append(n, pad);
	}

	template<typename Ret, typename Str>
	Ret PadRightAs(const Str& str, const std::uint32_t width, const typename Str::value_type pad)
	{
		auto buf = str;
		PadRightTo(buf, width, pad);
		if constexpr (std::is_same_v<Ret, Str>) return buf;
		return __Detail::GetStrFunc<Ret>{}(buf);
	}

	__Suit3(__PadImpl, PadRightAs, PadRight)
#pragma endregion Pad

#pragma region Combine
	template<typename Str, typename...Args>
	void CombineTo(Str& str, Args&&... args)
	{
		(str.append(args), ...);
	}

	template<typename Ret, typename...Args>
	Ret CombineAs(Args&&... args)
	{
		Ret buf{};
		CombineTo(buf, std::forward<Args>(args)...);
		return buf;
	}

#define __CombineImpl(type, func)\
	template<typename...Args>\
	type func(Args&&... args)\
		{ return CombineAs<type>(std::forward<Args>(args)...); }

	__Suit2(__CombineImpl, Combine);
#pragma endregion Combine

#pragma region FromStream
	template<typename T, typename...Args>
	void FromStreamTo(std::string& str, const T& toStream, Args&&...fmt)
	{
		std::ostringstream buf{};
		(buf << ... << fmt) << toStream;
		str.append(buf.str());
	}

	template<typename Ret, typename T, typename...Args>
	Ret FromStreamAs(const T& toStream, Args&&...fmt)
	{
		std::string buf{};
		FromStreamTo(buf, toStream, std::forward<Args>(fmt)...);
		if constexpr (std::is_same_v<Ret, std::string>) return buf;
		return __Detail::GetStrFunc<Ret>{}(buf);
	}
	
#define __FromStreamImpl(type, func)\
	template<typename T, typename...Args>\
	type func(const T& toStream, Args&&...fmt)\
		{ return FromStreamAs<type>(toStream, std::forward<Args>(fmt)...); }

	__Suit2(__FromStreamImpl, FromStream)
#pragma endregion FromStream

#pragma region Join
	template<typename It, typename Str, typename Out>
	void JoinTo(Out& str, It beg, It end, const Str& seq)
	{
		auto i = beg;
		if (end - beg == 0) return;
		for (; i < end - 1; ++i)
		{
			str.append(*i);
			str.append(seq);
		}
		str.append(*i);
	}

	template<typename Ret, typename It, typename Str>
	Ret JoinAs(It beg, It end, const Str& seq)
	{
		Ret buf{};
		JoinTo(buf, beg, end, seq);
		return buf;
	}
	
#define __JoinImpl(type, func)\
	template<typename It, typename Str>\
	type func(It beg, It end, const Str& seq)\
		{ return JoinAs<type>(beg, end, seq); }

	__Suit2(__JoinImpl, Join)
#pragma endregion Join

#pragma region ToString
	template<typename Str, typename T>
	Str ToStringAs(const T& t) { return __Detail::GetStrFunc<Str>()(__Detail::ToStringImpl(t)); }

	template<typename T> std::string ToString(const T& t) { return ToStringAs<std::string>(t); }
	template<typename T> std::wstring ToWString(const T& t) { return ToStringAs<std::wstring>(t); }
#ifdef __U8stringUseChar8
    template<typename T> std::u8string ToU8String(const T& t) { return ToStringAs<std::u8string>(t); }
#endif
	template<typename T> std::u16string ToU16String(const T& t) { return ToStringAs<std::u16string>(t); }
	template<typename T> std::u32string ToU32String(const T& t) { return ToStringAs<std::u32string>(t); }
#pragma endregion ToString
	
	namespace __Detail
	{
		template<typename StrVec, typename Arg>
		void ArgsToListImpl(StrVec& vec, const Arg& arg)
		{
			vec.push_back(ToStringAs<typename StrVec::value_type>(arg));
		}

		template<typename StrVec, typename... Args>
		void ArgsToList(StrVec& out, Args&&...args)
		{
			(ArgsToListImpl(out, std::forward<Args>(args)), ...);
		}
	}

#pragma region Format
	template<typename Str, typename FmtStr, typename... Args>
	void FormatTo(Str& str, const FmtStr& fmtStr, Args&&...args)
	{
		std::vector<Str> argsStr{};
		const auto fmt = std::filesystem::path(fmtStr).u32string();
		__Detail::ArgsToList(argsStr, std::forward<Args>(args)...);
		const auto token = U"{}";
		std::u32string::size_type start = 0;
		auto pos = fmt.find(token, start);
		uint64_t i = 0;
		while (pos != decltype(fmt)::npos)
		{
			str.append(ToStringAs<Str>(fmt.substr(start, pos - start)));
			str.append(argsStr.at(i++));
			start = pos + 2;
			pos = fmt.find(token, start);
		}
		str.append(ToStringAs<Str>(fmt.substr(start)));
	}

	template<typename Ret, typename FmtStr, typename... Args>
	Ret FormatAs(const FmtStr& fmtStr, Args&&...args)
	{
		Ret buf{};
		FormatTo(buf, fmtStr, std::forward<Args>(args)...);
		return buf;
	}

#define __FormatImpl(type, func)\
	template<typename FmtStr, typename... Args>\
	type func(const FmtStr& fmtStr, Args&&...args)\
		{ return FormatAs<type>(fmtStr, std::forward<Args>(args)...); }

	__Suit2(__FormatImpl, Format)
#pragma endregion Format

#pragma region Utf8
	template<typename Str>
	auto ToUtf8(const Str& str) { return std::filesystem::path(str).u8string(); }

	template<typename Str>
	Str FromUtf8As(const std::string& utf8)
    {
        return __Detail::GetStrFunc<Str>{}(
#ifdef __U8stringUseChar8
            std::filesystem::path
#else
            std::filesystem::u8path
#endif
                (utf8));
    }

#define __FromUtf8Impl(type, func)\
	inline type func(const std::string& utf8) { return FromUtf8As<type>(utf8); }

	__Suit2(__FromUtf8Impl, FromUtf8)
#pragma endregion Utf8
}

#undef NewStringImpl
#undef NewString
