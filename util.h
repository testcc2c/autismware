#pragma once

namespace util {

	// pre-declare.
	class datamap_t;

	// prototype.
	using inputfunc_t = void(__cdecl*)(void* data);

	enum fieldtype_t {
		FIELD_VOID = 0,			// No type or value
		FIELD_FLOAT,			// Any floating point value
		FIELD_STRING,			// A string ID (return from ALLOC_STRING)
		FIELD_VECTOR,			// Any vector, QAngle, or AngularImpulse
		FIELD_QUATERNION,		// A quaternion
		FIELD_INTEGER,			// Any integer or enum
		FIELD_BOOLEAN,			// boolean, implemented as an int, I may use this as a hint for compression
		FIELD_SHORT,			// 2 byte integer
		FIELD_CHARACTER,		// a byte
		FIELD_COLOR32,			// 8-bit per channel r,g,b,a (32bit color)
		FIELD_EMBEDDED,			// an embedded object with a datadesc, recursively traverse and embedded class/structure based on an additional typedescription
		FIELD_CUSTOM,			// special type that contains function pointers to it's read/write/parse functions
		FIELD_CLASSPTR,			// CBaseEntity *
		FIELD_EHANDLE,			// Entity handle
		FIELD_EDICT,			// edict_t *
		FIELD_POSITION_VECTOR,	// A world coordinate (these are fixed up across level transitions automagically)
		FIELD_TIME,				// a floating point time (these are fixed up automatically too!)
		FIELD_TICK,				// an integer tick count( fixed up similarly to time)
		FIELD_MODELNAME,		// Engine string that is a model name (needs precache)
		FIELD_SOUNDNAME,		// Engine string that is a sound name (needs precache)
		FIELD_INPUT,			// a list of inputed data fields (all derived from CMultiInputVar)
		FIELD_FUNCTION,			// A class function pointer (Think, Use, etc)
		FIELD_VMATRIX,			// a vmatrix (output coords are NOT worldspace)
		FIELD_VMATRIX_WORLDSPACE,// A VMatrix that maps some local space to world space (translation is fixed up on level transitions)
		FIELD_MATRIX3X4_WORLDSPACE,	// matrix3x4_t that maps some local space to world space (translation is fixed up on level transitions)
		FIELD_INTERVAL,			// a start and range floating point interval ( e.g., 3.2->3.6 == 3.2 and 0.4 )
		FIELD_MODELINDEX,		// a model index
		FIELD_MATERIALINDEX,	// a material index (using the material precache string table)
		FIELD_VECTOR2D,			// 2 floats
		FIELD_TYPECOUNT,		// MUST BE LAST
	};

	enum {
		TD_OFFSET_NORMAL = 0,
		TD_OFFSET_PACKED = 1,
		TD_OFFSET_COUNT,
	};

	class typedescription_t {
	public:
		fieldtype_t				m_type;
		const char* m_name;
		int						m_offset[TD_OFFSET_COUNT];
		unsigned short			m_size;
		short					m_flags;
		const char* m_ext_name;
		void* m_save_restore_ops;
		inputfunc_t				m_input_func;
		datamap_t* m_td;
		int						m_bytes;
		typedescription_t* m_override_field;
		int						m_override_count;
		float					m_tolerance;
	private:
		PAD(0x8);
	};

	class datamap_t {
	public:
		typedescription_t* m_desc;
		int					m_size;
		char const* m_name;
		datamap_t* m_base;
		bool		        m_chains_validated;
		bool				m_packed_offsets_computed;
		int			        m_packed_size;
	};

	__forceinline Address copy(Address dst, Address src, size_t size) {
		__movsb(
			dst.as<uint8_t*>(),
			src.as<uint8_t*>(),
			size
		);

		return dst;
	}

	// memset
	__forceinline Address set(Address dst, uint8_t val, size_t size) {
		__stosb(
			dst.as<uint8_t*>(),
			val,
			size
		);

		return dst;
	}

	template< typename o = void*, typename i = void* >
	__forceinline o force_cast(i in) {
		union { i in; o out; }
		u = { in };
		return u.out;
	};

	template < typename t = Address >
	__forceinline static t get_method(Address this_ptr, size_t index) {
		return (t)this_ptr.to< t* >()[index];
	}

	// get base ptr ( EBP (x86_32) / RBP (x86_64) ).
	__forceinline uintptr_t GetBasePointer() {
		return (uintptr_t)_AddressOfReturnAddress() - sizeof(uintptr_t);
	}

	// wide -> multi-byte
	__forceinline std::string WideToMultiByte(const std::wstring& str) {
		std::string ret;
		int         str_len;

		// check if not empty str
		if (str.empty())
			return {};

		// count size
		str_len = g_winapi.WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), 0, 0, 0, 0);

		// setup return value
		ret = std::string(str_len, 0);

		// final conversion
		g_winapi.WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), &ret[0], str_len, 0, 0);

		return ret;
	}

	// multi-byte -> wide
	__forceinline std::wstring MultiByteToWide(const std::string& str) {
		std::wstring    ret;
		int		        str_len;

		// check if not empty str
		if (str.empty())
			return {};

		// count size
		str_len = g_winapi.MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);

		// setup return value
		ret = std::wstring(str_len, 0);

		// final conversion
		g_winapi.MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &ret[0], str_len);

		return ret;
	}

	__forceinline int FindInDataMap(datamap_t* pMap, const char* name) {
		while (pMap)
		{
			for (int i = 0; i < pMap->m_size; i++)
			{
				if (pMap->m_desc[i].m_name == NULL)
					continue;

				if (strcmp(name, pMap->m_desc[i].m_name) == 0)
					return pMap->m_desc[i].m_offset[TD_OFFSET_NORMAL];

				if (pMap->m_desc[i].m_type == FIELD_EMBEDDED)
				{
					if (pMap->m_desc[i].m_td)
					{
						unsigned int offset;

						if ((offset = FindInDataMap(pMap->m_desc[i].m_td, name)) != 0)
							return offset;
					}
				}
			}
			pMap = pMap->m_base;
		}

		return 0;
	}
};