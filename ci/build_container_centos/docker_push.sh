#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
set -e

# push the envoy image on merge to master
want_push='false'
for branch in "master"
do
   if [ "$TRAVIS_BRANCH" == "$branch" ]
   then
       want_push='true'
   fi
done
if [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$want_push" == "true" ]
then
    if [[ $(git diff HEAD^ ci/build_container_centos/) ]]; then
        echo "There are changes in the ci/build_container_centos directory"
        echo "Updating lyft/envoy-build-centos image"
        cd ci/build_container_centos
        ./docker_build.sh
        docker login -u $DOCKERHUB_USERNAME -p $DOCKERHUB_PASSWORD
        docker push lyft/envoy-build-centos:$TRAVIS_COMMIT
        docker tag lyft/envoy-build-centos:$TRAVIS_COMMIT lyft/envoy-build-centos:latest
        docker push lyft/envoy-build-centos:latest
    else
        echo "The ci/build_container_centos directory has not changed"
    fi
else
    echo 'Ignoring PR branch for docker push.'
fi
