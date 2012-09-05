#sbs-git:slp/pkgs/t/tel-plugin-imcmodem
Name:       tel-plugin-imcmodem
Summary:    telephony plugin library for AT communication with IMC modem
Version:    0.1.2
Release:    1
Group:      System/Libraries
License:    Apache
Source0:    tel-plugin-imcmodem-%{version}.tar.gz
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  cmake
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(tcore)

%description
imcmodem plugin for telephony

%prep
%setup -q

%build
cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix}
make %{?jobs:-j%jobs}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
rm -rf %{buildroot}
%make_install

%files
%defattr(-,root,root,-)
#%doc COPYING
%{_libdir}/telephony/plugins/*
