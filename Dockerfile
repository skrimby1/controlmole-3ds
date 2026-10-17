# devkitARM plus CTRPluginFramework and 3gxtool, for the Luma plugin in plugin/.
FROM devkitpro/devkitarm:latest
RUN printf '\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional\n\n[thepixellizeross-linux]\nServer = https://thepixellizeross.gitlab.io/packages/x86_64/linux\nSigLevel = Optional\n' >> /opt/devkitpro/pacman/etc/pacman.conf \
 && dkp-pacman -Sy --noconfirm libctrpf 3gxtool \
 && dkp-pacman -Scc --noconfirm
