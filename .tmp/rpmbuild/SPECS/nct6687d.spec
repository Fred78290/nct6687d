%global pkgver 62
%global commithash f59feef

Name:           nct6687d
Version:        1.0.%{pkgver}
Release:        git%{commithash}
Summary:        Kernel module (kmod) for %{prjname}
License:        GPL-2.0
URL:            https://github.com/Fred78290/nct6687d
Source0:        nct6687.conf

# For kmod package
Provides:       %{name}-kmod-common = %{version}-%{release}
Requires:       %{name}-kmod >= %{version}

BuildArch:      noarch

%description
%{prjname} kernel module

%prep

%build
# Nothing to build

%install

install -D -m 0644 %{SOURCE0} %{buildroot}%{_modulesloaddir}/nct6687.conf

%files
%{_modulesloaddir}/nct6687.conf

%changelog
* Wed Jan 04 2023 Frederic BOLTZ <frederic.boltz@gmail.com> - %{version}
- Initial package
