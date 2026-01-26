#!/bin/bash

EXOSENS_PREFIX="/opt/exosens"
EXOSENS_LIB="${EXOSENS_PREFIX}/lib/aarch64-linux-gnu"

if [[ x$2 == x ]]
then
   # Build on target
   if [[ $1 == "make" ]]
   then
      # === Build libcamera ===
      if [ ! -d libcamera_build ]
      then
         echo "=== Cloning libcamera ==="
         git clone --depth 1 --branch "v0.6.0+rpt20251202" https://github.com/raspberrypi/libcamera libcamera_build
         pushd libcamera_build
         git apply ../0001-Add-support-for-exosens-cameras-libcamera.patch
         echo "=== Configuring libcamera for ${EXOSENS_PREFIX} ==="
         meson setup build --prefix=${EXOSENS_PREFIX} \
            -Dpipelines=simple,rpi/vc4,rpi/pisp \
            -Dipas=rpi/vc4,rpi/pisp \
            -Dv4l2=true \
            -Dcam=enabled \
            -Dtest=false
         popd
      fi

      echo "=== Building libcamera ==="
      pushd libcamera_build
      ninja -C build
      popd

      # === Build rpicam-apps ===
      if [ ! -d rpicam-apps-exosens ]
      then
         echo "=== Cloning rpicam-apps ==="
         git clone --depth 1 --branch "v1.11.0" https://github.com/raspberrypi/rpicam-apps.git rpicam-apps-exosens
         git apply ../0002-Add-support-for-exosens-cameras-rpicam-apps.patch
      fi

      echo "=== Configuring rpicam-apps with Exosens libcamera ==="
      pushd rpicam-apps-exosens

      # Check if build directory exists
      if [ ! -d build ]; then
         # Configure with pkg-config path pointing to Exosens installation
         # and RPATH so binaries can find libraries without LD_LIBRARY_PATH
         PKG_CONFIG_PATH="${EXOSENS_LIB}/pkgconfig" \
         meson setup build \
            --prefix=${EXOSENS_PREFIX} \
            -Dc_link_args="-Wl,-rpath,${EXOSENS_LIB}" \
            -Dcpp_link_args="-Wl,-rpath,${EXOSENS_LIB}" \
            -Denable_libav=disabled \
            -Denable_drm=enabled \
            -Denable_egl=disabled \
            -Denable_qt=disabled \
            -Denable_opencv=disabled \
            -Denable_tflite=disabled
      fi

      echo "=== Building rpicam-apps ==="
      ninja -C build
      popd

   elif [[ $1 == "install" ]]
   then
      # === Install libcamera ===
      if [ -d libcamera_build ]
      then
         echo "=== Installing libcamera to ${EXOSENS_PREFIX} ==="
         pushd libcamera_build
         sudo ninja -C build install
         popd

         # Update library cache
         echo "=== Updating library cache ==="
         echo "${EXOSENS_LIB}" | sudo tee /etc/ld.so.conf.d/exosens-libcamera.conf
         sudo ldconfig
      fi

      # === Install rpicam-apps ===
      if [ -d rpicam-apps-exosens ]
      then
         echo "=== Installing rpicam-apps to ${EXOSENS_PREFIX} ==="
         pushd rpicam-apps-exosens
         sudo ninja -C build install
         popd

         # Create wrapper scripts with -exosens suffix
         echo "=== Creating wrapper scripts ==="
         sudo mkdir -p /usr/local/bin

         for app in cam rpicam-hello rpicam-still rpicam-vid rpicam-raw rpicam-jpeg rpicam-detect; do
            if [ -f "${EXOSENS_PREFIX}/bin/${app}" ]; then
               echo "Creating ${app}-exosens wrapper..."
               sudo tee /usr/local/bin/${app}-exosens > /dev/null << EOF
#!/bin/bash
export LD_LIBRARY_PATH="${EXOSENS_LIB}:$LD_LIBRARY_PATH"
export LIBCAMERA_IPA_FORCE_ISOLATION=0
export LIBCAMERA_IPA_MODULE_PATH="${EXOSENS_LIB}/libcamera/ipa"
export LIBCAMERA_IPA_CONFIG_PATH="${EXOSENS_PREFIX}/share/libcamera/ipa"
exec ${EXOSENS_PREFIX}/bin/${app} "\$@"
EOF
               sudo chmod +x /usr/local/bin/${app}-exosens
            fi
         done

         echo "=== Installation complete ==="
         echo ""
         echo "Exosens libcamera installed in: ${EXOSENS_PREFIX}"
         echo "Binaries available:"
         echo "  - cam-exosens"
         echo "  - rpicam-hello-exosens"
         echo "  - rpicam-still-exosens"
         echo "  - rpicam-vid-exosens"
         echo "  - rpicam-raw-exosens"
         echo "  - rpicam-jpeg-exosens"
         echo "  - rpicam-detect-exosens"
         echo ""
         echo "Test with: rpicam-hello-exosens --list-cameras"
      fi

   elif [[ $1 == "clean" ]]
   then
      echo "=== Cleaning build directories ==="
      rm -rf libcamera_build
      rm -rf rpicam-apps-exosens
      echo "Done. To remove installed files, run:"
      echo "  sudo rm -rf ${EXOSENS_PREFIX}"
      echo "  sudo rm /etc/ld.so.conf.d/exosens-libcamera.conf"
      echo "  sudo ldconfig"
      echo "  sudo rm /usr/local/bin/rpicam-*-exosens"
   fi
fi
