// dlopen/dlsym for LLVM's JIT in a PS5 title.
//
// A title cannot dlopen itself: dlopen(NULL) fails and dlerror() answers a pointer the
// title cannot read (LLVM's DynamicLibrary::getPermanentLibrary copied it and faulted).
// RPCS3's JIT links its code against the process through DynamicLibrary: here the process
// handle answers from a table of what JIT-compiled code calls (libc, libm, compiler-rt).

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

extern "C"
{
	// compiler-rt builtins LLVM lowers 128-bit and half-float operations to
	__int128 __divti3(__int128, __int128);
	__int128 __modti3(__int128, __int128);
	unsigned __int128 __udivti3(unsigned __int128, unsigned __int128);
	unsigned __int128 __umodti3(unsigned __int128, unsigned __int128);
	__int128 __multi3(__int128, __int128);
	float __extendhfsf2(uint16_t);
	uint16_t __truncsfhf2(float);
	uint16_t __truncdfhf2(double);
	double __floattidf(__int128);
	double __floatuntidf(unsigned __int128);
	__int128 __fixdfti(double);
	unsigned __int128 __fixunsdfti(double);
	float __floattisf(__int128);
	float __floatuntisf(unsigned __int128);
	__int128 __fixsfti(float);
	unsigned __int128 __fixunssfti(float);
}

namespace
{
	struct symbol
	{
		const char* name;
		void* address;
	};

#define SYM(x) { #x, reinterpret_cast<void*>(&x) }
#define SYM_AS(name, fn) { name, reinterpret_cast<void*>(fn) }

	const symbol s_symbols[] =
	{
		SYM(memcpy), SYM(memmove), SYM(memset), SYM(memcmp), SYM(strlen),
		SYM(malloc), SYM(free), SYM(abort),
		SYM_AS("fmod", static_cast<double(*)(double, double)>(&::fmod)),
		SYM_AS("fmodf", static_cast<float(*)(float, float)>(&::fmodf)),
		SYM_AS("sin", static_cast<double(*)(double)>(&::sin)),
		SYM_AS("sinf", static_cast<float(*)(float)>(&::sinf)),
		SYM_AS("cos", static_cast<double(*)(double)>(&::cos)),
		SYM_AS("cosf", static_cast<float(*)(float)>(&::cosf)),
		SYM_AS("tan", static_cast<double(*)(double)>(&::tan)),
		SYM_AS("tanf", static_cast<float(*)(float)>(&::tanf)),
		SYM_AS("exp", static_cast<double(*)(double)>(&::exp)),
		SYM_AS("expf", static_cast<float(*)(float)>(&::expf)),
		SYM_AS("exp2", static_cast<double(*)(double)>(&::exp2)),
		SYM_AS("exp2f", static_cast<float(*)(float)>(&::exp2f)),
		SYM_AS("log", static_cast<double(*)(double)>(&::log)),
		SYM_AS("logf", static_cast<float(*)(float)>(&::logf)),
		SYM_AS("log2", static_cast<double(*)(double)>(&::log2)),
		SYM_AS("log2f", static_cast<float(*)(float)>(&::log2f)),
		SYM_AS("log10", static_cast<double(*)(double)>(&::log10)),
		SYM_AS("log10f", static_cast<float(*)(float)>(&::log10f)),
		SYM_AS("pow", static_cast<double(*)(double, double)>(&::pow)),
		SYM_AS("powf", static_cast<float(*)(float, float)>(&::powf)),
		SYM_AS("sqrt", static_cast<double(*)(double)>(&::sqrt)),
		SYM_AS("sqrtf", static_cast<float(*)(float)>(&::sqrtf)),
		SYM_AS("floor", static_cast<double(*)(double)>(&::floor)),
		SYM_AS("floorf", static_cast<float(*)(float)>(&::floorf)),
		SYM_AS("ceil", static_cast<double(*)(double)>(&::ceil)),
		SYM_AS("ceilf", static_cast<float(*)(float)>(&::ceilf)),
		SYM_AS("trunc", static_cast<double(*)(double)>(&::trunc)),
		SYM_AS("truncf", static_cast<float(*)(float)>(&::truncf)),
		SYM_AS("round", static_cast<double(*)(double)>(&::round)),
		SYM_AS("roundf", static_cast<float(*)(float)>(&::roundf)),
		SYM_AS("rint", static_cast<double(*)(double)>(&::rint)),
		SYM_AS("rintf", static_cast<float(*)(float)>(&::rintf)),
		SYM_AS("nearbyint", static_cast<double(*)(double)>(&::nearbyint)),
		SYM_AS("nearbyintf", static_cast<float(*)(float)>(&::nearbyintf)),
		SYM_AS("fma", static_cast<double(*)(double, double, double)>(&::fma)),
		SYM_AS("fmaf", static_cast<float(*)(float, float, float)>(&::fmaf)),
		SYM_AS("fmin", static_cast<double(*)(double, double)>(&::fmin)),
		SYM_AS("fminf", static_cast<float(*)(float, float)>(&::fminf)),
		SYM_AS("fmax", static_cast<double(*)(double, double)>(&::fmax)),
		SYM_AS("fmaxf", static_cast<float(*)(float, float)>(&::fmaxf)),
		SYM_AS("ldexp", static_cast<double(*)(double, int)>(&::ldexp)),
		SYM_AS("ldexpf", static_cast<float(*)(float, int)>(&::ldexpf)),
		SYM(__divti3), SYM(__modti3), SYM(__udivti3), SYM(__umodti3), SYM(__multi3),
		SYM(__extendhfsf2), SYM(__truncsfhf2), SYM(__truncdfhf2),
		SYM(__floattidf), SYM(__floatuntidf), SYM(__fixdfti), SYM(__fixunsdfti),
		SYM(__floattisf), SYM(__floatuntisf), SYM(__fixsfti), SYM(__fixunssfti),
	};

#undef SYM
#undef SYM_AS

	// Any non-null value: the process, as dlopen(NULL) names it
	char s_process_handle;

	thread_local const char* s_error = nullptr;
}

extern "C"
{
	void* dlopen(const char* path, int /*mode*/)
	{
		if (!path)
		{
			s_error = nullptr;
			return &s_process_handle;
		}

		s_error = "dlopen: a PS5 title cannot load libraries";
		return nullptr;
	}

	void* dlsym(void* handle, const char* name)
	{
		if (handle == &s_process_handle || handle == nullptr || handle == reinterpret_cast<void*>(-2) /* RTLD_DEFAULT */)
		{
			const std::string_view wanted(name ? name : "");

			for (const symbol& s : s_symbols)
			{
				if (wanted == s.name)
				{
					s_error = nullptr;
					return s.address;
				}
			}
		}

		s_error = "dlsym: symbol not found";
		return nullptr;
	}

	char* dlerror(void)
	{
		const char* error = s_error;
		s_error = nullptr;
		return const_cast<char*>(error);
	}

	int dlclose(void* /*handle*/)
	{
		return 0;
	}
}
