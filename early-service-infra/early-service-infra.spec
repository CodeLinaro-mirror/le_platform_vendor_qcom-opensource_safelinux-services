Name: early-service-infra
Version: 1.0
Release: r0
Summary: systemd target for early services
License: BSD-3-Clause-Clear
Source0: %{name}-%{version}.tar.gz
BuildRequires: cmake gcc-c++ systemd systemd-rpm-macros pkgconfig pkgconfig(bootkpi-logging)
%{?systemd_requires}

%description
This systemd target is a synchronization point for all early services. Early services shall be configured to start Before=early-services.target

%prep
%setup -qn %{name}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
systemctl enable early-services.target

%preun
%systemd_preun early-services.target

%postun
%systemd_postun early-services.target

%files
%{_unitdir}/early-services.target
