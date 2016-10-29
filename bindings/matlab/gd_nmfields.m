function nfields = gd_nmfields(D, parent)
% GD_NMFIELDS  Retrieve a count of fields
%
%   GD_NMFIELDS(DIRFILE,PARENT)
%             is equivalent to calling GD_NENTRIES(DIRFILE,GD.ALL_FRAGMENTS, ...
%                                                                    PARENT,0,0)
%
%   For GD, see GETDATA_CONSTANTS.
%
%   See also GD_NENTRIES, GETDATA_CONSTANTS

  GD = getdata_constants();
  nfields = gd_nentries(D, GD.ALL_FRAGMENTS, parent);
end

% Copyright (C) 2013, 2016 D. V. Wiebe
%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
% This file is part of the GetData project.
%
% GetData is free software; you can redistribute it and/or modify it under
% the terms of the GNU Lesser General Public License as published by the
% Free Software Foundation; either version 2.1 of the License, or (at your
% option) any later version.
%
% GetData is distributed in the hope that it will be useful, but WITHOUT
% ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
% FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
% License for more details.
%
% You should have received a copy of the GNU Lesser General Public License
% along with GetData; if not, write to the Free Software Foundation, Inc.,
% 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
