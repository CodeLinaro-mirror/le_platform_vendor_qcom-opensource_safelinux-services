Name: power-utils
Version: 1.0
Release: r0
Summary: userspace power utilities
License: BSD-3-Clause-Clear
URL: https://www.codelinaro.org/
Source0: %{name}-%{version}.tar.gz
BuildRequires: cmake gcc gcc-c++ glibc-devel systemd-rpm-macros pkgconfig pkgconfig(libsystemd)
%{?systemd_requires}

%description
Utilities to support userspace suspend and shutdown

%prep
%setup -n %{name}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
systemctl enable check-inhibitors.service

%preun
%systemd_preun check-inhibitors.service

%postun
%systemd_postun_with_restart check-inhibitors.service

%files
%{_bindir}/check-inhibitors
%{_unitdir}/check-inhibitors.service
