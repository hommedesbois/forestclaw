c     # This is the "classic" call to setaux.
      subroutine setaux(maxmx,maxmy,mbc,mx,my,xlower,ylower,
     &      dx,dy,maux,aux)
      implicit none

      integer mbc, mx,my, meqn, maux, maxmx, maxmy
      double precision dx,dy, xlower, ylower
      double precision  aux(1-mbc:mx+mbc,1-mbc:my+mbc, maux)

      double precision t

      call compute_velocity_psi(mx,my,mbc,dx,dy,
     &      xlower,ylower,t,aux,maux)

      end

      subroutine compute_velocity_psi(mx,my,mbc,
     &      dx,dy,xlower,ylower,t,aux,maux)
      implicit none

      integer mx,my,mbc,maux
      integer blockno
      double precision dx,dy, t, xlower,ylower

      double precision xd1(3),xd2(3)
      double precision aux(1-mbc:mx+mbc,1-mbc:my+mbc,maux)

      integer i,j
      double precision vn, get_vel_psi

      do i = 1-mbc,mx+mbc
         do j = 1-mbc,my+mbc
c           # x-faces
            xd1(1) = xlower + (i-1)*dx
            xd1(2) = ylower + (j)*dy
            xd1(3) = 0

            xd2(1) = xlower + (i-1)*dx
            xd2(2) = ylower + (j-1)*dy
            xd2(3) = 0

            vn = get_vel_psi(xd1,xd2,dy)
            aux(i,j,1) = vn
         enddo
      enddo

      do j = 1-mbc,my+mbc
         do i = 1-mbc,mx+mbc
c           # y-faces
            xd1(1) = xlower + i*dx
            xd1(2) = ylower + (j-1)*dy
            xd1(3) = 0

            xd2(1) = xlower + (i-1)*dx
            xd2(2) = ylower + (j-1)*dy
            xd2(3) = 0

            vn = get_vel_psi(xd1,xd2,dx)
            aux(i,j,2) = -vn
         enddo
      enddo

      end

      subroutine set_capacity(mx,my,mbc,dx,dy,area,maux,aux)
      implicit none

      integer mx,my,mbc,maux
      double precision dx,dy
      double precision aux(1-mbc:mx+mbc,1-mbc:my+mbc,maux)

      double precision area(-mbc:mx+mbc+1,-mbc:my+mbc+1)

      integer i,j
      double precision dxdy

      dxdy = dx*dy

      do i = 1-mbc,mx+mbc
         do j = 1-mbc,my+mbc
            aux(i,j,3) = area(i,j)/dxdy
         enddo
      enddo




      end
