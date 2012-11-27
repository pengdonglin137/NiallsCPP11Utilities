/* NiallsCPP11Utilities
(C) 2012 Niall Douglas http://www.nedprod.com/
File Created: Nov 2012
*/

#ifndef NIALLSCPP11UTILITIES_H
#define NIALLSCPP11UTILITIES_H

/*! \file NiallsCPP11Utilities.hpp
\brief Declares Niall's useful C++ 11 utilities
*/

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251) // needs to have dll-interface to be used by clients of
#endif


/*! \mainpage

\warning You'll definitely need a fairly compliant C++ 11 compiler for this library to work.
In particular, you \b need variadic templates.

Build using scons (http://www.scons.org/). You only need to build StaticTypeRegistry.cpp
if you use NiallsCPP11Utilities::StaticTypeRegistry (it consists of one line unavoidable
on Windows). You can use --useclang to force use of clang. You can use --usegcc to force
use of gcc on Windows. I have configured scons to have the intelligence to try using g++
if it's not being run from inside a Visual Studio Tools Command Box (i.e. vcvars32.bat
hasn't been run). Finally, --debugbuild generates a debug build ;). There are a few more
build options available too, try 'scons --help' to see them.

Tested on the following compilers:
 - Visual Studio 2012 Nov CTP (the one with variadic template support)
 - clang++ v3.2.
 - g++ v4.6.2.
*/

#include <cassert>

#include <vector>
#include <memory>
#include <type_traits>
#include <functional>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <typeinfo>
#include <string>

#if defined(_MSC_VER) && _MSC_VER<=1700 && !defined(noexcept)
#define noexcept throw()
#endif
#if defined(_MSC_VER) && _MSC_VER<=1700 && !defined(constexpr)
#define constexpr const
#endif
#if defined(__GNUC__) && !defined(GCC_VERSION)
#define GCC_VERSION (__GNUC__ * 10000 \
				   + __GNUC_MINOR__ * 100 \
				   + __GNUC_PATCHLEVEL__)
#endif

//! \define DLLEXPORTMARKUP The markup this compiler uses to export a symbol from a DLL
#ifndef DLLEXPORTMARKUP
#ifdef WIN32
#define DLLEXPORTMARKUP __declspec(dllexport)
#elif defined(__GNUC__)
#define DLLEXPORTMARKUP __attribute__((visibility("default")))
#else
#define DLLEXPORTMARKUP
#endif
#endif

//! \define DLLIMPORTMARKUP The markup this compiler uses to import a symbol from a DLL
#ifndef DLLIMPORTMARKUP
#ifdef WIN32
#define DLLIMPORTMARKUP __declspec(dllimport)
#else
#define DLLIMPORTMARKUP
#endif
#endif

//! \define DLLSELECTANYMARKUP The markup this compiler uses to mark a symbol as being weak
#ifndef DLLWEAKMARKUP
#ifdef WIN32
#define DLLWEAKMARKUP(type, name) extern __declspec(selectany) type name; extern __declspec(selectany) type name##_weak=NULL; __pragma(comment(linker, "/alternatename:_" #name "=_" #name "_weak"))
#elif defined(__GNUC__)
#define DLLWEAKMARKUP(type, name) extern __attribute__((weak)) type declaration
#else
#define DLLWEAKMARKUP(type, name)
#endif
#endif

#ifdef NIALLSCPP11UTILITIES_DLL_EXPORTS
#define NIALLSCPP11UTILITIES_API DLLEXPORTMARKUP
#else
#define NIALLSCPP11UTILITIES_API DLLIMPORTMARKUP
#endif

//! \define DEFINES Defines RETURNS to automatically figure out your return type
#ifndef RETURNS
#define RETURNS(...) -> decltype(__VA_ARGS__) { return (__VA_ARGS__); }
#endif

//! \namespace NiallsCPP11Utilities Where Niall's useful C++ 11 utilities live
namespace NiallsCPP11Utilities {

/*! \brief Defines a byte buffer processing std::streambuf

Use like this:
\code
char foo[5];
membuf mb(foo, sizeof(foo));
std::istream reader(&mb);
\endcode
*/
struct membuf : public std::streambuf
{
    membuf(char *s, size_t n)
    {
        setg(s, s, s + n);
    }
};

namespace Impl {
	template<typename T, bool iscomparable> struct is_nullptr { bool operator()(T c) const noexcept { return !c; } };
	template<typename T> struct is_nullptr<T, false> { bool operator()(T) const noexcept { return false; } };
}
//! Compile-time safe detector of if \em v is nullptr (can cope with non-pointer convertibles)
#if defined(__GNUC__) && GCC_VERSION<40700
template<typename T> bool is_nullptr(T v) noexcept { return Impl::is_nullptr<T, std::is_constructible<bool, T>::value>()(std::forward<T>(v)); }
#else
template<typename T> bool is_nullptr(T v) noexcept { return Impl::is_nullptr<T, std::is_trivially_constructible<bool, T>::value>()(std::forward<T>(v)); }
#endif

namespace Impl {
	template<bool isTemplated, typename T> struct has_regular_call_operator
	{
	  template<typename C> // detect regular operator()
	  static char test(decltype(&C::operator()));

	  template<typename C> // worst match
	  static char (&test(...))[2];

	  static constexpr bool value = (sizeof( test<T>(0)  ) == 1);
	};

	template<typename T> struct has_regular_call_operator<true,T>
	{
	  static constexpr bool value = true;
	};
}

template<typename T> struct has_call_operator
{
  template<typename F, typename A> // detect 1-arg operator()
  static char test(int, decltype( (*(F*)0)( (*(A*)0) ) ) = 0);

  template<typename F, typename A, typename B> // detect 2-arg operator()
  static char test(int, decltype( (*(F*)0)( (*(A*)0), (*(B*)0) ) ) = 0);

  template<typename F, typename A, typename B, typename C> // detect 3-arg operator()
  static char test(int, decltype( (*(F*)0)( (*(A*)0), (*(B*)0), (*(C*)0) ) ) = 0);

  template<typename F, typename ...Args> // worst match
  static char (&test(...))[2];

  static constexpr bool OneArg = (sizeof( test<T, int>(0)  ) == 1);
  static constexpr bool TwoArg = (sizeof( test<T, int, int>(0)  ) == 1);
  static constexpr bool ThreeArg = (sizeof( test<T, int, int, int>(0)  ) == 1);

  static constexpr bool HasTemplatedOperator = OneArg || TwoArg || ThreeArg;
  static constexpr bool value = Impl::has_regular_call_operator<HasTemplatedOperator, T>::value;
};

template<typename callable> class UndoerImpl
{
	callable undoer;
	bool _dismissed;

#if !defined(_MSC_VER) || _MSC_VER>1700
	UndoerImpl() = delete;
	UndoerImpl(const UndoerImpl &) = delete;
	UndoerImpl &operator=(const UndoerImpl &) = delete;
#else
	UndoerImpl();
	UndoerImpl(const UndoerImpl &);
	UndoerImpl &operator=(const UndoerImpl &);
#endif
	explicit UndoerImpl(callable &&c) : undoer(std::move(c)), _dismissed(false) { }
	void int_trigger() { if(!_dismissed && !is_nullptr(undoer)) { undoer(); _dismissed=true; } }
public:
	UndoerImpl(UndoerImpl &&o) : undoer(std::move(o.undoer)), _dismissed(o._dismissed) { o._dismissed=true; }
	UndoerImpl &operator=(UndoerImpl &&o) { int_trigger(); undoer=std::move(o.undoer); _dismissed=o._dismissed; o._dismissed=true; return *this; }
	template<typename _callable> friend UndoerImpl<_callable> Undoer(_callable c);
	~UndoerImpl() { int_trigger(); }
	//! Returns if the Undoer is dismissed
	bool dismissed() const { return _dismissed; }
	//! Dismisses the Undoer
	void dismiss(bool d=true) { _dismissed=d; }
	//! Undismisses the Undoer
	void undismiss(bool d=true) { _dismissed=!d; }
};


/*! \brief Alexandrescu style rollbacks, a la C++ 11.

Example of usage:
\code
auto resetpos=Undoer([&s]() { s.seekg(0, std::ios::beg); });
...
resetpos.dismiss();
\endcode
*/
template<typename callable> inline UndoerImpl<callable> Undoer(callable c)
{
	//static_assert(!std::is_function<callable>::value && !std::is_member_function_pointer<callable>::value && !std::is_member_object_pointer<callable>::value && !has_call_operator<callable>::value, "Undoer applied to a type not providing a call operator");
	auto foo=UndoerImpl<callable>(std::move(c));
	return foo;
}

namespace Impl {
	typedef std::unordered_map<size_t, std::map<std::string, void *>> ErasedTypeRegistryMapType;
	extern NIALLSCPP11UTILITIES_API ErasedTypeRegistryMapType *static_type_registry_storage;

	template<class _registry, class _type, class _containertype> struct StaticTypeRegistryStorage
	{
		typedef _registry registry;
		typedef _type type;
		typedef _containertype containertype;
		static containertype **registryStorage()
		{
			static containertype **_registryStorage; // Keep a local cache
			if(!_registryStorage)
			{
				const std::type_info &typeinfo=typeid(containertype);
				// This deliberately and intentionally leaks because we have no way of knowing when to clean it up
				if(!static_type_registry_storage)
					static_type_registry_storage=new ErasedTypeRegistryMapType;
				auto &typemap=(*static_type_registry_storage)[typeinfo.hash_code()];
				auto &containerstorage=typemap[typeinfo.name()];
				if(!containerstorage)
				{
					auto container=new containertype();
					containerstorage=static_cast<void *>(container);
					//assert(typemap[typeinfo.name()]==static_cast<void *>(container));
				}
				_registryStorage=(containertype **) &containerstorage;
			}
			return _registryStorage;
		}
		static void RegisterData(const type &c)
		{
			(*registryStorage())->push_back(c);
		}
		static void RegisterData(type &&c)
		{
			(*registryStorage())->push_back(std::move(c));
		}
		static void UnregisterData(const type &c)
		{
			auto _r=registryStorage();
			auto r=*_r;
			// Quick optimisation for tail pop to avoid a search
			if(*r->rbegin()==c)
				r->erase(--r->end());
			else
				r->erase(std::remove(r->begin(), r->end(), c), r->end());
			if(r->empty())
			{
				delete r;
				*_r=nullptr;
			}
		}
	};
}
/*! \brief An iterable, statically stored registry of items associated with a type

Only one of these ever exists in the process, so you can always iterate like this:
\code
typedef StaticTypeRegistry<Foo, std::unique_ptr<Foo>(*)()> MakeablesRegistry;
for(auto n : MakeablesRegistry())
   ...
\endcode

To use this you must compile StaticTypeRegistry.cpp.

\sa NiallsCPP11Utilities::RegisterData(), NiallsCPP11Utilities::AutoDataRegistration()
*/
template<class _registry, class _type, class _containertype=std::vector<_type>> struct StaticTypeRegistry
{
private:
	_containertype &__me() { auto r=Impl::StaticTypeRegistryStorage<_registry, _type, _containertype>::registryStorage(); return **r; }
	const _containertype &__me() const { auto r=Impl::StaticTypeRegistryStorage<_registry, _type, _containertype>::registryStorage(); return **r; }
public:
	operator _containertype &() { return __me(); }
	operator const _containertype &() const { return __me(); }
	typename _containertype::iterator begin() { return __me().begin(); }
	typename _containertype::const_iterator begin() const { return __me().begin(); }
	typename _containertype::const_iterator cbegin() const { return __me().cbegin(); }
	typename _containertype::iterator end() { return __me().end(); }
	typename _containertype::const_iterator end() const { return __me().end(); }
	typename _containertype::const_iterator cend() const { return __me().cend(); }
	typename _containertype::iterator rbegin() { return __me().rbegin(); }
	typename _containertype::const_iterator rbegin() const { return __me().rbegin(); }
	typename _containertype::iterator rend() { return __me().rend(); }
	typename _containertype::const_iterator rend() const { return __me().rend(); }
	typename _containertype::size_type size() const { return __me().size(); }
	typename _containertype::size_type max_size() const { return __me().max_size(); }
	bool empty() const { return __me().empty(); }
};

namespace Impl {
	template<class typeregistry> struct RegisterDataImpl;
	template<class _registry, class _type, class _containertype> struct RegisterDataImpl<StaticTypeRegistry<_registry, _type, _containertype>>
	{
		typedef _registry registry;
		typedef _type type;
		typedef _containertype containertype;
		static void Do(_type &&v)
		{
			Impl::StaticTypeRegistryStorage<_registry, _type, _containertype>::RegisterData(std::forward<_type>(v));
		}
	};
	template<class typeregistry> struct UnregisterDataImpl;
	template<class _registry, class _type, class _containertype> struct UnregisterDataImpl<StaticTypeRegistry<_registry, _type, _containertype>>
	{
		typedef _registry registry;
		typedef _type type;
		typedef _containertype containertype;
		static void Do(const _type &v)
		{
			Impl::StaticTypeRegistryStorage<_registry, _type, _containertype>::UnregisterData(v);
		}
	};
}
//! Registers a piece of data with the specified type registry
template<class typeregistry> inline void RegisterData(const typename Impl::RegisterDataImpl<typeregistry>::type &v)
{
	Impl::RegisterDataImpl<typeregistry>::Do(v);
}
//! Registers a piece of data with the specified type registry
template<class typeregistry> inline void RegisterData(typename Impl::RegisterDataImpl<typeregistry>::type &&v)
{
	Impl::RegisterDataImpl<typeregistry>::Do(std::forward<typename Impl::RegisterDataImpl<typeregistry>::type>(v));
}
//! Unregisters a piece of data with the specified type registry
template<class typeregistry> inline void UnregisterData(const typename Impl::UnregisterDataImpl<typeregistry>::type &v)
{
	Impl::UnregisterDataImpl<typeregistry>::Do(v);
}

namespace Impl {
	template<class _typeregistry> struct DataRegistration;
	template<class _registry, class _type, class _containertype> struct DataRegistration<StaticTypeRegistry<_registry, _type, _containertype>>
	{
		typedef StaticTypeRegistry<_registry, _type, _containertype> _typeregistry;
		DataRegistration(_type &&_c) : c(std::move(_c)) { RegisterData<_typeregistry>(std::forward<_type>(c)); }
		~DataRegistration() { UnregisterData<_typeregistry>(std::forward<_type>(c)); }
	private:
		_type c;
	};
}
/*! \brief Auto registers a data item with a type registry. Typically used at static init/deinit time.

Per DLL object:
\code
typedef StaticTypeRegistry<Foo, std::unique_ptr<Foo>(*)()> MakeablesRegistry;
static auto reg=AutoDataRegistration<MakeablesRegistry>(&Goo::Make);
\endcode
This registers the Goo::Make callable with the registry MakeablesRegistry during DLL load. It also unregisters during DLL unload.

You now have a registry of static Make() methods associated with type MakeablesRegistry. To iterate:
\code
for(auto n : StaticTypeRegistry<MakeablesRegistry>())
   ...
\endcode
*/
template<class _typeregistry, class _type> inline Impl::DataRegistration<_typeregistry> AutoDataRegistration(_type &&c)
{
	return Impl::DataRegistration<_typeregistry>(std::move(c));
}

/*! \brief Information about mapped files in the process

This is not a fast call, on any system. On Linux and FreeBSD this call returns
a perfect snapshot - on Windows and Mac OS X, there is a slight possibility that
data returned is incomplete or contains spurious data as the set of mapped files
may change mid-traversal.

To use this you must compile MappedFileInfo.cpp and ErrorHandling.cpp.

\sa NiallsCPP11Utilities::FromCodePoint()
*/
struct NIALLSCPP11UTILITIES_API MappedFileInfo
{
	std::string path;				//!< Full path to the binary.
	size_t startaddr, endaddr;		//!< Start and end addresses of where it's mapped to
	off_t offset;					//!< From which offset in the file
	size_t length;					//!< Length of mapped section (basically \c endaddr-startaddr)
	bool read, write, execute, copyonwrite;	//!< Reflecting if the section is readable, writeable, executable and/or copy-on-write

	bool operator<(const MappedFileInfo &o) const noexcept { return startaddr<o.startaddr; }
	bool operator==(const MappedFileInfo &o) const noexcept { return startaddr==o.startaddr && endaddr==o.endaddr && read==o.read && write==o.write && execute==o.execute && copyonwrite==o.copyonwrite && path==o.path; }
	//! Returns a snapshot of mapped sections in the process
	static std::map<size_t, MappedFileInfo> mappedFiles();
};
//! \brief Finds the MappedFileInfo containing code point \codepoint, if any
template<class R, class... Pars> inline std::map<size_t, MappedFileInfo>::const_iterator FromCodePoint(const std::map<size_t, MappedFileInfo> &list, R(*codepoint)(Pars...))
{
	size_t addr=(size_t)(void *)codepoint;
	auto it=list.lower_bound(addr);
	if(it->first>addr) --it;
	if(it->second.startaddr<=addr && it->second.endaddr>addr)
		return it;
	return list.cend();
}

} // namespace


#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif