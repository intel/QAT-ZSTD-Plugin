Name:		qat-zstd-plugin
Version:	0.2.0
Release:	%autorelease
Summary:	Intel QuickAssist Technology ZSTD Plugin

License:	BSD-3-Clause
URL:		https://github.com/intel/QAT-ZSTD-Plugin
Source0:	%{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:	automake
BuildRequires:	gcc
BuildRequires:	libzstd-devel
BuildRequires:	qatlib-devel

# Upstream only supports x86_64
ExclusiveArch:	x86_64

%description
Intel QuickAssist Technology ZSTD is a plugin to Zstandard for accelerating
compression by QAT. ZSTD* is a fast lossless compression algorithm, targeting
real-time compression scenarios at zlib-level and better compression ratios.

%package       devel
Summary:       Headers and libraries of QAT-ZSTD-Plugin
Requires:      %{name}%{?_isa} = %{version}-%{release}

%description   devel
This package contains headers and libraries required to build applications
that use the QAT ZSTD Plugin.

%prep
%autosetup -p1 -n QAT-ZSTD-Plugin-%{version}

%build
%make_build
make

%install
make install LIBDIR=%{buildroot}%{_libdir} INCLUDEDIR=%{buildroot}%{_includedir}
rm %{buildroot}%{_libdir}/libqatseqprod.a

%files
%license LICENSE*
%{_libdir}/libqatseqprod.so

%files         devel
%{_libdir}/libqatseqprod.so
%{_includedir}/qatseqprod.h

%changelog
* Tue Jul 30 2024 Zhu Chengfei <chengfeix.zhu@intel.com> - 0.2.0-1
- Update to qat-zstd-plugin v0.2.0
