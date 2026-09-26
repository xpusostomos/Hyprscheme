#pragma once

/*
    The C++ <-> Guile boundary: every entry point the Scheme API calls is
    registered here as a gsubr — a real Guile primitive taking and returning
    SCM.

    The C functions keep ordinary C++ signatures (long long, const char*,
    double); bind<Fn>() wraps one in a trampoline that unboxes the arguments
    and boxes the result, so call sites stay readable and there is no second
    dialect to learn. Arguments are validated on the way in: a wrong type or
    count raises a Scheme error — it never reaches compositor code as a bad
    pointer.

    (Registered with a rest argument: Guile then hands the arguments over as a
    list, so one trampoline shape covers every arity and the check is ours.)

    THREAD RULE: every op must run on the interpreter's thread — the
    compositor's event-loop thread that ran init().
*/

#include <libguile.h>

#include <cstdlib>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace hl {

    // ---- small string helpers (the boundary's own) ---------------------------

    // byte-exact string contents
    inline std::string toStdString(SCM v) {
        size_t      len = 0;
        char*       buf = scm_to_utf8_stringn(v, &len);
        std::string out(buf, len);
        free(buf);
        return out;
    }

    inline std::string symbolName(SCM v) {
        return toStdString(scm_symbol_to_string(v));
    }

    // a string OR a symbol, the way the API's coercion paths want it
    inline std::string toName(SCM v) {
        return scm_is_symbol(v) ? symbolName(v) : toStdString(v);
    }

    // real but not an exact integer (what the API means by "a float value")
    inline bool isInexact(SCM v) {
        return scm_is_real(v) && !scm_is_exact_integer(v);
    }

    // a Scheme error raised from C: unwinds to the nearest handler, which the
    // machinery's guards always are — it cannot cross compositor frames. The
    // messages name the argument, not the function: the gsubr's own name isn't
    // reachable from a plain function pointer, and the Scheme wrappers do the
    // user-facing validation anyway.
    inline void fail(const char* message, SCM irritant = SCM_UNDEFINED) {
        scm_misc_error("hyprscheme", message, scm_is_eq(irritant, SCM_UNDEFINED) ? SCM_EOL : scm_list_1(irritant));
    }

    // ---- argument conversion -------------------------------------------------
    // How to turn one SCM into the C type, keeping any storage (a std::string
    // for const char*) alive for the duration of the call.
    template <typename T>
    struct Arg;

    template <>
    struct Arg<SCM> {
        using Store = SCM;
        static Store from(SCM v, size_t) { return v; }
        static SCM   pass(const Store& s) { return s; }
    };

    template <>
    struct Arg<long long> {
        using Store = long long;
        static Store from(SCM v, size_t i) {
            if (!scm_is_integer(v))
                fail("argument ~A is not an integer", scm_from_size_t(i));
            return scm_to_int64(v);
        }
        static long long pass(const Store& s) { return s; }
    };

    template <>
    struct Arg<int> {
        using Store = int;
        static Store from(SCM v, size_t i) {
            if (!scm_is_integer(v))
                fail("argument ~A is not an integer", scm_from_size_t(i));
            return scm_to_int(v);
        }
        static int pass(const Store& s) { return s; }
    };

    template <>
    struct Arg<unsigned long long> {
        using Store = unsigned long long;
        static Store from(SCM v, size_t i) {
            if (!scm_is_integer(v))
                fail("argument ~A is not an integer", scm_from_size_t(i));
            return scm_to_uint64(v);
        }
        static unsigned long long pass(const Store& s) { return s; }
    };

    template <>
    struct Arg<double> {
        using Store = double;
        static Store from(SCM v, size_t i) {
            if (!scm_is_real(v))
                fail("argument ~A is not a number", scm_from_size_t(i));
            return scm_to_double(v);
        }
        static double pass(const Store& s) { return s; }
    };

    template <>
    struct Arg<const char*> {
        using Store = std::string;
        static Store from(SCM v, size_t i) {
            if (!scm_is_string(v))
                fail("argument ~A is not a string", scm_from_size_t(i));
            return toStdString(v);
        }
        static const char* pass(const Store& s) { return s.c_str(); }
    };

    // ---- return conversion ---------------------------------------------------
    template <typename R>
    struct Box {
        static SCM out(R v) { return v; }   // SCM-returning entry points
    };
    template <>
    struct Box<int> {
        static SCM out(int v) { return scm_from_int(v); }
    };
    template <>
    struct Box<long long> {
        static SCM out(long long v) { return scm_from_int64(v); }
    };
    template <>
    struct Box<double> {
        static SCM out(double v) { return scm_from_double(v); }
    };
    template <>
    struct Box<bool> {
        static SCM out(bool v) { return v ? SCM_BOOL_T : SCM_BOOL_F; }
    };
    template <>
    struct Box<void> {
        static SCM out() { return SCM_UNSPECIFIED; }
    };

    // ---- the trampoline ------------------------------------------------------
    // One instantiation per registered function: unpack the argument list,
    // unbox, call, box.
    template <auto Fn, typename Sig>
    struct Tramp;

    template <auto Fn, typename R, typename... A>
    struct Tramp<Fn, R (*)(A...)> {
        static SCM call(SCM arglist) {
            std::vector<SCM> given;
            for (SCM l = arglist; scm_is_pair(l); l = scm_cdr(l))
                given.push_back(scm_car(l));
            if (given.size() != sizeof...(A))
                fail("expected ~A argument(s)", scm_from_size_t(sizeof...(A)));
            return unbox(std::index_sequence_for<A...>{}, given);
        }

      private:
        template <size_t... I>
        static SCM unbox(std::index_sequence<I...>, const std::vector<SCM>& given) {
            auto stored = std::make_tuple(Arg<A>::from(given[I], I + 1)...);
            if constexpr (std::is_void_v<R>) {
                Fn(Arg<A>::pass(std::get<I>(stored))...);
                return SCM_UNSPECIFIED;
            } else
                return Box<R>::out(Fn(Arg<A>::pass(std::get<I>(stored))...));
        }
    };

    // build a Scheme list from SCM values (the handle collections)
    inline SCM listOf(const std::vector<SCM>& vals) {
        SCM l = SCM_EOL;
        for (auto it = vals.rbegin(); it != vals.rend(); ++it)
            l = scm_cons(*it, l);
        return l;
    }

    // ---- keeping values alive across a C++ container -------------------------
    // Boehm scans the C stack and statics, but NOT malloc'd C++ containers: a
    // value held only in a std::vector<SCM> can be collected mid-build, so
    // builders pin each element while it sits in the container and release the
    // whole batch once the value is finished.
    inline void pin(SCM v, std::vector<SCM>& roots) {
        roots.push_back(v);
        scm_gc_protect_object(v);
    }
    inline void unpin(std::vector<SCM>& roots) {
        for (auto it = roots.rbegin(); it != roots.rend(); ++it)
            scm_gc_unprotect_object(*it);
        roots.clear();
    }

    // ---- registration --------------------------------------------------------
    template <auto Fn>
    static SCM gsubrTramp(SCM arglist) {
        return Tramp<Fn, decltype(Fn)>::call(arglist);
    }

    template <auto Fn>
    inline void bind(const char* name) {
        scm_c_define_gsubr(name, 0, 0, 1, reinterpret_cast<scm_t_subr>(&gsubrTramp<Fn>));
    }

} // namespace hl
