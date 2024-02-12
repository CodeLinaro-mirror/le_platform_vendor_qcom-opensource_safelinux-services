Name: suspend-resume-DSPs
Version: 1.0
Release: r0
Summary: suspend and resume DSPs
License: BSD-3-Clause-Clear
URL: https://www.codelinaro.org/
Source0: %{name}-%{version}.tar.gz
BuildRequires: gcc-c++ cmake systemd systemd-rpm-macros
%{?systemd_requires}

%description
%{name} - %{summary} - systemd services responsible for suspending and resuming
DSPs as part of end-to-end systemd suspend design

# We must set debug_package to nil because there are no "source code" files to
# create debug symbols from.
%global debug_package %{nil}

%prep
%setup -n %{name}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
# Disable these services until they are required for suspend design
#systemctl enable suspend-DSPs.service
#systemctl enable resume-DSPs.service

%preun
#%%systemd_preun suspend-DSPs.service
#%%systemd_preun resume-DSPs.service

%postun
#%%systemd_postun suspend-DSPs.service
#%%systemd_postun resume-DSPs.service

%files
%{_unitdir}/suspend-DSPs.service
%{_unitdir}/resume-DSPs.service
