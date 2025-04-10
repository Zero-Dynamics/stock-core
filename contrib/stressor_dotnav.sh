#!/bin/bash


# This file serves the purpose of testing the network behavior under extreme load of
# random transactions and tokens/nfts transactions.
# Blocks are being mined through proof of stake process so this script usually takes hours
# to finish depending on the setup. Tests that are included are:
#
# 	1. Create random transactions
# 	2. Register names
# 	3. Update names and subdomain names
# 	4. Renew names
#       5. Check all nodes resolve names the same way
# 	6. Perform random verifychain checks to simulate network reorg back to genesis
# 	7. Randomly turn off nodes and then turn them back on
# 	8. Randomly boot up a fresh node to test syncing from genesis block
# 	9. Randomly create new network topologies to loosely connect the nodes
# 	10. Split the network down into user defined number of subnetworks and perform tests
# 	    1-5, re-combine all subnetworks afterwards into 1 to see if all networks follow
# 	    the longest chain correctly
# 	11. Final verifychain check back to genesis
#

# NOTE: This stresser is written in bash(4.4) environment. OSX users may need to install gshuf and substitude all shuf with gshuf.

### Need to configure this!!

### Path to binaries
stockpath=../src

### How many voting cycles to run the stresser
cycles=40

### Number of nodes to start. (Setting node_count=1 will overwrite settings for stressing_node_count, array_stressing_nodes, array_verifychain_nodes)
node_count=8

### Number of nodes to be creating proposals, consultations, and  voting.
stressing_node_count=6

### If manual selection of stressing nodes is preferred, specify below, or leave it an empty array. If specified, node count must match stressing_node_count
array_stressing_nodes=()

### Nodes to check verifychain, 8 random nodes will be picked if left empty "()", more nodes make verifychain process faster but try to use no more than 8 nodes
array_verifychain_nodes=()

### How many extra blocks should be staked when the stresser is done
extra_blocks_to_stake=30
extra_blocks_to_stake_randomness=30


### Length in seconds for a stress round
seconds_round=60

### Network topology, a random topology will be generated if left empty "()", or specify nodes to be connect by pairs (EX to have a network with topology of
###
### 0-1-2-8- 9-10
### | | |    |  |
### 3 4 5   11-12
###   | |    |  |
###   6-7   13-14
###             |
###            15
###
### specify the array as ("0 1" "1 2" "2 8" "8 9" "9 10" "0 3" "1 4" "2 5" "9 11" "10 12" "11 12" "4 6" "5 7" "11 13" "12 14" "6 7" "13 14" "14 15")
array_topology=()

###How many networ to split into
network_count=4

### How many voting cycels to run in the splitting network phase
cycles_network_split=40

### How long the sleep time is in a stress round ( say a stress round is 60 sec, sleep 4 sec after all the tests are run once)
stress_sleep_time=4


#################### Advanced Settings ##################

###Set to 1 to active the test
bool_random_tx=1
bool_random_verifychain_check=0
bool_stopstart_nodes=1
bool_random_new_topology=1
bool_sync_new_node=1
bool_network_split=1


#######################################################################################Functions

function initialize_network {
	array_p2p_port=()
	array_rpc_port=()
	array_data=()
	array_user=()
	array_pwd=()
}

function initialize_node {
	array_p2p_port[$1]=$( bc <<< "10000+$1" )
	array_rpc_port[$1]=$( bc <<< "30000+$1" )
	data_=$(mktemp -d)
	array_data[$1]=$data_
	array_user[$1]=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-_\$\?" < /dev/urandom | head -c 10)
	array_pwd[$1]=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-_\$\?" < /dev/urandom | head -c 10)
	echo "-datadir=${array_data[$1]} -rpcport=${array_rpc_port[$1]}"
	start_node $1
	array_active_nodes[$1]=$1
	array_all_nodes[$1]=$1
}

function remove_node {
	unset array_active_nodes[$1]
	unset array_all_nodes[$1]
	unset array_data[$1]
	unset array_user[$1]
	unset array_pwd[$1]
	unset array_p2p_port[$1]
	unset array_rpc_port[$1]
	unset array_stopped_nodes[$1]
}


trap ctrl_c INT

function ctrl_c() {
	terminate 1
}

function copy_array {
	declare -n array=$1
	unset $2
	for i in ${!array[@]};
	do
		eval "$2[$i]=\"${array[$i]}\""
	done
}


function stock_cli {
	$stockpath/stock-cli -datadir=${array_data[$1]} -rpcport=${array_rpc_port[$1]} -devnet $2 $3 $4 $5 $6 2> /dev/null
}

function terminate {
	if [ "$1" == 1 ];
	then
		fail_logs_D=$(echo "/tmp/stresser_fail_logs_$RANDOM")
		mkdir $fail_logs_D
		for i in $(seq 0 1 $( bc <<< "$node_count-1") );
		do
			folder_name=$(echo "${array_data[$i]}" | cut -c 10-)
			stock_cli $i listproposals > ${array_data[$i]}/devnet/listproposals.out
			stock_cli $i listconsultations > ${array_data[$i]}/devnet/listconsultations.out
			cp ${array_data[$i]}/devnet/debug.log $fail_logs_D/debug-out-node$i-$folder_name
			cp ${array_data[$i]}/devnet/listproposals.out $fail_logs_D/listproposals.out-node$i-$folder_name
			cp ${array_data[$i]}/devnet/listconsultations.out $fail_logs_D/listconsultations.out-node$i-$folder_name
		done
		echo debug.log files, listconsultations.out, listproposals.out files are copied to $fail_logs_D
	fi
	for i in ${array_active_nodes[@]};
	do
		stock_cli $i stop
	done
	echo "Stopping all nodes..."
	sleep 30
	echo Done
	killall -9 stockd
	exit $1
}

function connect_nodes {
	stock_cli "$1" "addnode 127.0.0.1:$( bc <<< 10000+$2 ) add"
	stock_cli "$1" "addnode 127.0.0.1:$( bc <<< 10000+$2 ) onetry"
}

function disconnect_nodes {
	stock_cli "$1" "addnode 127.0.0.1:$( bc <<< 10000+$2 ) remove"
	stock_cli "$1" "disconnectnode 127.0.0.1:$( bc <<< 10000+$2 )"
}


function select_random_nodes {
	if [ "$2" == 1 ];
	then
		array_random_nodes=($(shuf -i 1-$( bc <<< "$node_count-1" ) -n $1))
	else
		array_random_nodes=($(shuf -i 0-$( bc <<< "$node_count-1" ) -n $1))
	fi
}

function shuffle_array {
	local array=("$@")
	shuffled_array=($(shuf -e ${array[@]}))
}


#Establish topology using set variable $network_topology
function connect_network {
	local local_array=("$@")
	if [ "${#local_array[@]}" -lt 1 ];
	then
		return
	fi
	for i in ${!local_array[@]};
	do
		local connection_pair=(${local_array[$i]})
		out=$(connect_nodes ${connection_pair[0]} ${connection_pair[1]})
	done
	sleep 1
	check_connection "${local_array[@]}"
}

function disconnect_network {
	local local_array=("$@")
	for i in ${!local_array[@]};
	do
		local connection_pair=(${local_array[$i]})
		out=$(disconnect_nodes ${connection_pair[0]} ${connection_pair[1]})
	done
	sleep 5
}

#Checks if topology is correctly connected
function check_connection {
	local array=("$@")
	check_connection_done=0
	for i in ${!array[@]};
	do
		local connection_pair=(${array[$i]})
		check=$(stock_cli ${connection_pair[0]} getpeerinfo | grep "\"addr\": \"127.0.0.1:$( bc <<< "10000+${connection_pair[1]}" )\"")
		if [ -z "$check" ];
		then
			echo "Node ${connection_pair[0]} failed to connect to node ${connection_pair[1]}"
			echo " "
			echo "Trying to reconnect to all nodes...please wait"
			sleep 5
			connect_network "${array[@]}"
			break
		fi
	done
	if [ "$check_connection_done" == 0 ];
	then
		echo "All nodes are connected!"
		check_connection_done=1
	fi
}

function create_random_network_topology {
	unset array_topology_node_pairs
	local local_array_create_random_network_topology=("$@")
	if [ "${#local_array_create_random_network_topology[@]}" -eq 1 ];
	then
		return
	fi
	array_network_connection_pool=()
        for j in $(seq 0 1 $( bc <<< "${#local_array_create_random_network_topology[@]}-2" ));
        do
                for k in $(seq $( bc <<< "$j+1" ) 1 $( bc <<< "${#local_array_create_random_network_topology[@]}-1" ));
                do
                        array_network_connection_pool+=("${local_array_create_random_network_topology[$j]} ${local_array_create_random_network_topology[$k]}")
                done
        done
	if [ "${#local_array_create_random_network_topology[@]}" -lt 100 ];
	then
		network_density_upper_bound=$( echo $(echo "e(1.4*l(${#local_array_create_random_network_topology[@]}-1))" | bc -l)/1 | bc )
	else
		network_density_upper_bound=$( echo "${#local_array_create_random_network_topology[@]}*8" | bc )
	fi
	#network_denstiy is used to create a somewhat loosely connect network topology
	network_density_lower_bound=$( echo "${#local_array_create_random_network_topology[@]} - 1" | bc )
        network_density=$( shuf -i $network_density_lower_bound-$network_density_upper_bound -n 1 )
	array_topology_index=($(shuf -i 0-$( bc <<< "${#array_network_connection_pool[@]} - 1") -n $network_density))
	for i in $(seq 0 1 $( bc <<< "$network_density-1" ));
        do
		array_topology_node_pairs[$i]=${array_network_connection_pool[${array_topology_index[$i]}]}
	done

        check_network_connected array_topology_node_pairs

        while [ "${#array_connected_nodes[@]}" -lt "${#local_array_create_random_network_topology[@]}" ];
        do
                create_random_network_topology "${local_array_create_random_network_topology[@]}"
        done
}

function check_network_connected {
	unset array_to_be_checked_connected
	copy_array $1 array_to_be_checked_connected
        connection_progression_old=0
        connection_progression_new=2
	unset array_connected_nodes
	for i in ${array_to_be_checked_connected[@]};
	do
		if [ -n $i ];
		then
			array_connected_nodes=($i)
			break
		fi
	done
        while [ "$connection_progression_old" != "$connection_progression_new" ];
	do
                connection_progression_old=$connection_progression_new
		for i in ${!array_to_be_checked_connected[@]};
                do
			array_connection=(${array_to_be_checked_connected[$i]})
                        for j in ${!array_connected_nodes[@]};
                        do
                                within_network=0
                                if [ "${array_connection[0]}" == "${array_connected_nodes[$j]}" ];
                                then
                                        for k in $(seq 0 1 $( bc <<< " ${#array_connected_nodes[@]}-1" ));
                                        do
                                                if [ "${array_connection[1]}" == "${array_connected_nodes[$k]}" ];
                                                then
                                                        within_network=1
                                                fi
                                        done
                                        if [ "$within_network" == 0 ];
                                        then
                                                array_connected_nodes+=("${array_connection[1]}")
                                                array_to_be_checked_connected[$i]=""
                                                ((connection_progression_new++))
                                        fi
                                elif [ "${array_connection[1]}" == "${array_connected_nodes[$j]}" ];
                                then
                                        for k in $(seq 0 1 $( bc <<< " ${#array_connected_nodes[@]}-1" ));
                                        do
                                                if [ "${array_connection[0]}" == "${array_connected_nodes[$k]}" ];
                                                then
                                                        within_network=1
                                                fi
                                        done
                                        if [ "$within_network" == 0 ];
                                        then
                                                array_connected_nodes+=("${array_connection[0]}")
                                                array_to_be_checked_connected[$i]=""
                                                ((connection_progression_new++))
                                        fi
                                fi
                        done
                done
	done
}

function stop_selected_nodes {
	if [ "$attempts" == 10 ];
	then
		echo Cannot find suitable nodes to stop
		copy_array array_topology_node_pairs_backcup array_topology_node_pairs
		copy_array array_topology_node_pairs_stopped_backup array_topology_node_pairs_stopped
		return
	fi
	echo "Stopping nodes ${array_nodes_to_stop[@]}"
	for i in ${array_nodes_to_stop[@]};
	do
		stop_node $i
	done

	sleep 5
	for i in ${array_nodes_to_stop[@]};
	do
		unset array_active_nodes[$i]
		if [ -n ${array_stressing_nodes[$i]} ];
		then
			array_stopped_stressing_nodes[$i]=${array_stressing_nodes[$i]}
			unset array_stressing_nodes[$i]
		fi
		array_stopped_nodes[$i]=$i
	done
#	echo Currently active nodes are: ${array_active_nodes[@]} inactive ones are: ${array_stopped_nodes[@]}
#	echo array topology node pairs is ${array_topology_node_pairs[@]}
#	echo array topology node pairs stopped is ${array_topology_node_pairs_stopped[@]} with size of ${#array_topology_node_pairs_stopped[@]}

}

function select_random_nodes_to_stop {
	attempts=0
	copy_array array_topology_node_pairs array_topology_node_pairs_backcup
	copy_array array_topology_node_pairs_stopped array_topology_node_pairs_stopped_backup
	echo "Trying to find nodes to stop while avoiding network splitting..."
	unset array_connected_nodes
	while [ "${#array_connected_nodes[@]}" -lt "$( bc <<< "$node_count-$1" )" ] && [ "$attempts" -lt 10 ];
        do
		unset array_topology_node_pairs
		copy_array array_topology_node_pairs_backcup array_topology_node_pairs
		copy_array array_topology_node_pairs_stopped_backup array_topology_node_pairs_stopped
		((attempts++))
		shuffle_array "${array_active_nodes[@]}"
		unset array_nodes_to_stop
		for i in $(seq 0 1 $( bc <<< "$1 - 1" ));
		do
			array_nodes_to_stop[${shuffled_array[$i]}]=${shuffled_array[$i]}
		done
		check_stopping_nodes_cause_network_split "${array_nodes_to_stop[@]}"
        done
}

function check_stopping_nodes_cause_network_split {
	local local_array=("$@")
	for i in ${!array_topology_node_pairs[@]};
	do
		local node_pair=(${array_topology_node_pairs[$i]})
		for j in ${local_array[@]}
		do
			if [ "${node_pair[0]}" == "$j" ] || [ "${node_pair[1]}" == "$j" ];
			then
				array_topology_node_pairs_stopped[$i]=${array_topology_node_pairs[$i]}
				unset array_topology_node_pairs[$i]
				node_pair=()
			fi
		done
	done
	check_network_connected array_topology_node_pairs
}

function start_random_stopped_nodes {
	local local_array=()
	if [ ${#array_stopped_nodes[@]} == 0 ];
	then
		echo "All nodes are already active."
	else
		for i in ${!array_stopped_nodes[@]};
		do
			dice=$( bc <<< "$RANDOM % 2" )
			if [ "$dice" -eq 1 ];
			then
				local_array[$i]=$i
				start_stopped_node $i
			else
				echo skipping starting node $i
			fi
		done
		echo Waiting 30 seconds for stockd...
		sleep 30
		for j in ${!local_array[@]};
		do
			connect_node_to_network $j
			echo "Nodes $j balance: $(stock_cli $j getbalance) t0DYNS"
		done
	fi
	echo Currently active nodes are: ${array_active_nodes[@]} inactive ones are: ${array_stopped_nodes[@]}
}

function start_stopped_node {
	echo Starting node $1...
	start_node $1
	unset array_stopped_nodes[$1]
	array_active_nodes[$1]=$1
	if [ -n ${array_stopped_stressing_nodes[$1]} ];
	then
		array_stressing_nodes[$1]=${array_stopped_stressing_nodes[$1]}
		unset array_stopped_stressing_nodes[$1]
	fi
}

function connect_node_to_network {
	echo Connecting node $1 to the network...
	for i in ${!array_topology_node_pairs_stopped[@]};
	do
	local node_connection=(${array_topology_node_pairs_stopped[$i]})
		if [ "$1" == "${node_connection[0]}" ] || [ "$1" == "${node_connection[1]}" ];
		then
			local match=0
			for k in ${array_stopped_nodes[@]};
			do
				if [ "$k" == "${node_connection[0]}" ] || [ "$k" == "${node_connection[1]}" ];
				then
					match=1
				fi
			done
			if [ "$match" == 0 ] && [ -z ${array_topology_node_pairs[$i]} ];
			then
				array_topology_node_pairs[$i]=${array_topology_node_pairs_stopped[$i]}
				unset array_topology_node_pairs_stopped[$i]
			fi
		fi
	done
	connect_network "${array_topology_node_pairs[@]}"
}

function wait_until_sync {
	sleep 5
	local local_array=("$@")
	local local_array_best_hash=()
	wait_until_sync_done=0
	for i in ${local_array[@]};
	do
		local_array_best_hash[$i]=$(stock_cli $i getbestblockhash)
		echo best block hash of node $i:
		echo "$(stock_cli $i getbestblockhash)"
	done
	if [ $(printf "%s\n" "${local_array_best_hash[@]}" | LC_CTYPE=C sort -z -u | uniq | grep -n -c .) -gt 1 ];
	then
		echo best block hash mismatch!!!
#		echo array topoloy node pairs is "${array_topology_node_pairs[@]}"
#		echo array topology node pairs stopped is "${array_topology_node_pairs_stopped[@]}"
		sleep 1
		shuffle_array "${local_array[@]}"
		node=${shuffled_array[0]}
		$(stock_cli $node "generate 1")
		echo now on blcok $(stock_cli $node "getblockcount")
		if [ "$network_split_started" == 1 ];
		then
			for wusi in $(seq 0 1 $( bc <<< "$network_count-1" ));
			do
				echo Syncing network $wusi...
				eval "connect_network \"\${array_topology_node_pairs_network$nc[@]}\""
			done
		else
			connect_network "${array_topology_node_pairs[@]}"
		fi
		sleep 2
		wait_until_sync "${local_array[@]}"
	fi
	if [ "$wait_until_sync_done" == 0 ];
	then
		echo Best block hashes matched!
		wait_until_sync_done=1
	fi
}

function random_transactions {
	dice=$(bc <<< "$RANDOM % 100")
	if [ $dice -lt 75 ];
        then
		transaction_amount=$( bc <<< "$RANDOM%1000" )
		shuffle_array "${array_stressing_nodes[@]}"
		node_send=${shuffled_array[0]}
		node_receive=${shuffled_array[1]}
		if [ $dice -lt 30 ];
		then
			out=$(stock_cli ${array_stressing_nodes[$node_send]} sendtoaddress ${array_address[$node_receive]} $transaction_amount)
		else
			out=$(stock_cli ${array_stressing_nodes[$node_send]} privatesendtoaddress ${array_address[$node_receive]} $transaction_amount)
		fi
	fi
}

function join_by { local d=$1; shift; echo -n "$1"; shift; printf "%s" "${@/#/$d}"; }

function dice_register_name {
	dice=$(bc <<< "$RANDOM % 100")
	if [ $dice -lt 50 ];
	then
                shuffle_array "${array_stressing_nodes[@]}"
		namelength=$(bc <<< "$RANDOM %65")
                node=${shuffled_array[0]}
		random_name=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-\." < /dev/urandom | head -c $namelength)
#		random_symbol=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-_\$\?" < /dev/urandom | head -c 3)
		out=$(stock_cli $node registername $random_name.0dyns)
		if [ ! -z $out ]
		then
			echo 'create name success!'
			array_dotstocknames+=("$random_name.0dyns")
			echo 'dotstocknames= ' ${array_dotstocknames[@]}
		fi
	fi
}


function dice_update_name {
	dice=$(bc <<< "$RANDOM % 100")
	if [ $dice -lt 80 ];
	then
                shuffle_array "${array_stressing_nodes[@]}"
                node=${shuffled_array[0]}
		subdomainlength=$(bc <<< "$RANDOM % 20 + 1")
		for name in ${array_dotstocknames[@]}
		do
			random_key=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-\." < /dev/urandom | head -c $(bc <<< "$RANDOM % 10 + 1"))
			random_value=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-\." < /dev/urandom | head -c $(bc<<< "$RANDOM % 20+ 1"))
			random_subdomain=$(env LC_CTYPE=C tr -dc "a-zA-Z0-9-\." < /dev/urandom | head -c $(bc<<< "$RANDOM % $subdomainlength + 1"))
			if [ $(bc <<< "$RANDOM % 3") -lt 2 ];
			then
				out=$(stock_cli $node updatename $name $random_key $random_value)
				if [ ! -z $out ]
				then
					echo 'update name success!'
				fi
			else
				out=$(stock_cli $node updatename $random_subdomain.$name $random_key $random_value)
				if [ ! -z $out ]
				then
					echo 'update subdomain name success!'
					array_dotstocknames+=("$random_subdomain.$name")
					echo 'dotstocknames= ' ${array_dotstocknames[@]}
				fi
			fi
		done
	fi
}

function dice_renew_name {
	dice=$(bc <<< "$RANDOM % 100")
        if [ $dice -lt 50 ];
        then
		shuffle_array "${array_stressing_nodes[@]}"
		nodee=${shuffled_array[0]}
		shuffle_array "${array_dotstocknames[@]}"
		name=${shuffled_array[0]}
	        out=$(stock_cli $node renewname $name)
		if [ ! -z $out ]
		then
			echo 'renew name success!'
		fi
        fi

}

function check_dotstock_hash {
        sleep 1
        local local_array=("$@")
        local local_array_tokenhash=()
        for i in ${local_array[@]};
        do
		echo 'Node ' $i ' resolving names'
		local local_array_namehash=()
		for name in ${array_dotstocknames[@]}
		do
			echo 'resolving name' $name
			echo $(stock_cli $i resolvename $name)
			local_array_namehash+=($(stock_cli $i resolvename $name | jq -c |shasum|awk '{print $1}'))
		done
		local_array_tokenhash[i]=$(echo ${local_array_namehash[@]} | shasum|awk '{print $1}')
		echo best block hash of node $i:
		echo "$(stock_cli $i getbestblockhash)"
        done
	echo 'all registered names=' ${array_dotstocknames[@]}
	echo 'dot 0dyns hashes = ' ${local_array_tokenhash[@]}

        if [ $(printf "%s\n" "${local_array_tokenhash[@]}" | LC_CTYPE=C sort -z -u | uniq | grep -n -c .) -gt 1  ];
        then
                if [ "$bool_assert_state_mismatch" != 1 ];
                then
                        echo DOTSTOCK HASH MISMATCH! Syncing again to make sure best block hashes match.
                        bool_assert_state_mismatch=1
                        wait_until_sync "${local_array[@]}"
                        check_dotstock_hash "${local_array[@]}"
                else
                        echo DOTSTOCK HASH MISMATCH!
                        for i in ${!local_array_tokenhash[@]};
                        do
                                echo the dotstock hashes of nodes $i are ${local_array_tokenhash[$i]}
                        done
                        terminate 1
                fi
        fi
        echo DotStock hash check OK!
        bool_assert_state_mismatch=0
}


function check_consensus_parameters {
	consensusparameter_tmp=($(stock_cli $1 getconsensusparameters | tr -d "[],\n"))
	if [ "${#consensusparameter_tmp[@]}" -gt 10 ];
	then
		copy_array consensusparameter_tmp consensusparameter_new
	fi
	for i in $(seq 0 1 $( bc <<< "${#consensusparameter_new[@]}-1" ));
	do
		if [ "${consensusparameter_new[$i]}" != "${consensusparameter_old[$i]}" ];
		then
			echo Consensus Parameter ${consensus_parameter_name[$i]} changed from ${consensusparameter_old[$i]} to ${consensusparameter_new[$i]}!!!
		fi
		done
	consensusparameter_old=("${consensusparameter_new[@]}")
}

function stress {

	for i in ${array_stressing_nodes[@]};
	do
		out=$(stock_cli $i "staking true")
	done
	time=$(bc <<< $(date +%s)+$1)
	while [ $time -gt $(date +%s) ]
	do
		dice_register_name
		dice_update_name
		dice_renew_name
		if [ "$bool_random_tx" == 1 ];
		then
			random_transactions
		fi
		check_cycle
		sleep $stress_sleep_time
	done
}

function check_node {
	blocks=`stock_cli $2 getinfo|jq .blocks`
	block_increment=${#array_verifychain_nodes[@]}
	for i in $(seq $1 $block_increment $blocks);
	do
		verifyoutput=`stock_cli $2 verifychain 4 $i`
		if [[ "$verifyoutput" == "false" ]];
		then
			verifyoutput+=`echo ' - ' && echo failed at $(grep 'ERROR: VerifyDB()' ${array_data[$2]}/devnet/debug.log |tail -1|sed 's/.*block at \(\d*\)/\1/')`
			terminate 1
		fi
		echo Node $2 - Rewinding to \
		$(bc <<< $blocks-$i) \
		- reconnecting up to $blocks \
		- verifychain 4 $i -\> $verifyoutput;
	done
	verifyoutput=`stock_cli $2 verifychain 4 0`
	echo "Node $2 - Doing verifychain 4 0 -> $verifyoutput"
	echo "node $2 finished"
}

function check_cycle {
	shuffle_array "${array_stressing_nodes[@]}"
	node=${shuffled_array[0]}
	blocks=$(stock_cli $node getinfo|jq .blocks)
	if [ "$( bc <<< "$blocks - $last_cycle_end_block_height" )" -gt  "$voting_cycle_length" ];
	then
		last_cycle_end_block_height=$(echo "$last_cycle_end_block_height+$voting_cycle_length" | bc )
		voting_cycle_length=${consensusparameter_new[0]}
		((this_cycle++))
	fi
}

function random_verifychain_check {

	echo "Performing random verifycahin check..."

	echo "Selecting random verifychain nodes..."
	shuffle_array "${array_active_nodes[@]}"
	if [ "${#array_active_nodes[@]}" -lt 7 ];
	then
		for i in $(seq 0 1 $( bc <<< "${#array_active_nodes[@]} - 1" ));
		do
			array_verifychain_nodes[$i]=${shuffled_array[$i]}
		done
	else
		for i in $(seq 0 1 7 );
		do
			array_verifychain_nodes[$i]=${shuffled_array[$i]}
		done
	fi
	echo ''
	echo Running verifychain test with node ${array_verifychain_nodes[@]}...
	echo ''

	starting_block=0
	for i in ${!array_verifychain_nodes[@]};
	do
		((starting_block++))
		check_node $starting_block ${array_verifychain_nodes[$i]} &
	done
	wait
}

function start_node {
        $(echo $stockpath)/stockd -datadir=${array_data[$1]} -port=${array_p2p_port[$1]} -rpcport=${array_rpc_port[$1]} -devnet -daemon -debug=dao -debug=statehash -ntpminmeasures=-1 -dandelion=0 -disablesafemode -staking=0 2> /dev/null
#       gdb -batch -ex "run" -ex "bt" --args $(echo $stockpath)/stockd -datadir=${array_data[$1]} -port=${array_p2p_port[$1]} -rpcport=${array_rpc_port[$1]} -devnet -debug=dao -debug=statehash -ntpminmeasures=0 -dandelion=0 -disablesafemode -staking=0 > out.gdb &
}

function stop_node {
	out=$(stock_cli $1 stop)
}

##########################################################Script body

initialize_network

for i in $(seq 0 1 $( bc <<< "$node_count-1" ));
do
	initialize_node $i
done

echo ''
echo Waiting 30 seconds for stockd...

#Sleep to let stockd boot up, on slower systems the value may need to be much higher
sleep 30

if [ "$node_count" == 1 ];
then
	array_stressing_nodes=0
	array_verifychain_nodes=0
	stressing_node_count=1
else
	if [ -z $array_topology ];
	then
		echo "Creating random network topology..."
		create_random_network_topology "${array_active_nodes[@]}"
		echo "Created topology that has ${#array_topology_node_pairs[@]} connections out of max connections ${#array_network_connection_pool[@]} in the network"
		echo "Network topology: ${array_topology_node_pairs[@]}"
	else
		echo "Using speficyfied network topology ${array_topology_node_pairs[@]}"
	fi


	echo "running script with $stressing_node_count stressing nodes in a network of $node_count nodes"

	if [ -z $array_stressing_nodes ];
	then
		echo "Selecting random stressing nodes..."
		shuffle_array "${array_active_nodes[@]}"
		for i in $(seq 0 1 $( bc <<< "$stressing_node_count-1" ));
		do
			array_stressing_nodes[${shuffled_array[$i]}]=${shuffled_array[$i]}
		done
	else
		echo "Using specified stressing nodes"
	fi

	echo "array_stressing_nodes = ${array_stressing_nodes[@]}"

	if [ -z $array_verifychain_nodes ];
	then
		echo "Selecting random verifychain nodes..."
		shuffle_array "${array_active_nodes[@]}"
		for i in $(seq 0 1 7);
		do
			array_verifychain_nodes[$i]=${shuffled_array[$i]}
		done
	else
	        echo "Using specified verifychain nodes"
	fi

	echo "array_verifychain_nodes = ${array_verifychain_nodes[@]}"


	connect_network "${array_topology_node_pairs[@]}"
fi

out=$(stock_cli 0 "generate 10")

#Distribute funds to stressing nodes
array_address=()
for n in ${!array_stressing_nodes[@]};
do
        array_address[$n]=$(stock_cli ${array_stressing_nodes[$n]} getnewaddress)
done
for i in {1..10};
do
	for j in ${array_address[@]};
	do
		out=$(stock_cli 0 "sendtoaddress $j 10000")
	done
	out=$(stock_cli 0 "generate 1")
done
sleep 1
for n in ${array_stressing_nodes[@]};
do
	echo "Node $n balance: $(stock_cli $n getbalance) 0DYNS"
done


#Create blocks to make block count greater than 300
blocks=$(stock_cli 0 getblockcount)
echo 'blocks=' $blocks
while [ $blocks -lt 300 ];
do
	out=$(stock_cli 0 "generate 10")
	blocks=$(stock_cli 0 getblockcount)
done

echo 'Distributing x0DYNS to nodes...'

array_private_address=()
for n in ${!array_stressing_nodes[@]};
do
        array_private_address[$n]=$(stock_cli ${array_stressing_nodes[$n]} getnewprivateaddress)
done
for i in {1..10};
do
	for j in ${array_private_address[@]};
	do
		out=$(stock_cli 0 "sendtoaddress $j 10000")
	done
	out=$(stock_cli 0 "generate 1")
done
sleep 30
for n in ${array_stressing_nodes[@]};
do
	echo "Node $n balance: $(stock_cli $n getwalletinfo | jq '.private_balance') x0DYNS"
done

out=$(stock_cli 0 "generate 10")
donation=$(bc <<< "$RANDOM % 10000")
out=$(stock_cli 0 "donatefund $donation")

echo ''
echo Waiting until all nodes are synced

wait_until_sync "${array_active_nodes[@]}"

consensus_parameter_count=$(stock_cli 0 getconsensusparameters | jq length)
echo 'consensus parameter count =' $consensus_parameter_count
for i in $(seq 0 1 $( bc <<< "$consensus_parameter_count - 1" ));
do
	eval "consensus_parameter_name[\$i]=\$(stock_cli 0 \"getconsensusparameters true\" | jq -r '.[] | select(.id==$i) | .desc')"
done
consensusparameter_old=($(stock_cli 0 getconsensusparameters | tr -d "[],\n"))
consensusparameter_original=("${consensusparameter_old[@]}")
voting_cycle_length=${consensusparameter_old[0]}
blocks=$(stock_cli 0 getinfo|jq .blocks)
initial_cycle=$(bc <<< $blocks/$voting_cycle_length)
wait_until_cycle=$(bc <<< "$initial_cycle + $cycles")
this_cycle=$(bc <<< $blocks/$voting_cycle_length)
current_block=$(stock_cli 0 getinfo|jq .blocks)
last_cycle_end_block_height=$( bc <<< "$blocks - $blocks % $voting_cycle_length" )
shuffle_array "${array_stressing_nodes[@]}"
node=${shuffled_array[0]}
check_consensus_parameters $node
node_count_to_stop=$(echo "sqrt($node_count)" | bc )

while [ $wait_until_cycle -gt $this_cycle ]; do


	echo ''
	echo ''
	echo Stressing for $seconds_round seconds...

	stress $seconds_round

	echo Waiting until nodes are synced...

	wait_until_sync "${array_active_nodes[@]}"

	for i in ${array_stressing_nodes[@]};
	do
		out=$(stock_cli $i "staking false")
		echo "Node $i balance: $(stock_cli $i getwalletinfo | jq '.private_balance') x0DYNS"
	done

	echo Checking token hashes match...

	check_dotstock_hash "${array_active_nodes[@]}"
	shuffle_array "${array_stressing_nodes[@]}"
	node=${shuffled_array[0]}
	check_consensus_parameters $node

	if [ "$node_count" == 1 ];
	then
		out=$(stock_cli 0 "generate 5")
	fi

	if [ "$bool_random_verifychain_check" == 1 ];
	then
		dice=$( bc <<< "$RANDOM % 60" )
		if [ "$dice" == 1 ];
		then
			random_verifychain_check
		fi
	fi
	if [ "$bool_stopstart_nodes" == 1 ];
	then
		dice=$( bc <<< "$RANDOM % 3" )
		node_count_to_stop_this_round=$( bc <<< "$RANDOM % $node_count_to_stop + 1" )
		if [ "${#array_active_nodes[@]}" -gt "$( bc <<< "$node_count_to_stop_this_round + 2" )" ];
		then
			if [ "$dice" != 1 ];
			then
				select_random_nodes_to_stop $node_count_to_stop_this_round
				stop_selected_nodes
			fi
		fi
		if [ "$dice" == 1 ];
		then
			start_random_stopped_nodes
		fi
	fi
	if [ "$bool_random_new_topology" == 1 ];
	then
		dice=$( bc <<< "$RANDOM % 10" )
		if [ "$dice" == 1 ];
		then

			attempts=0
			copy_array array_topology_node_pairs array_topology_node_pairs_backcup
			copy_array array_topology_node_pairs_stopped array_topology_node_pairs_stopped_backup
			unset array_connected_nodes
			echo "Trying to create a new network topology while avoiding network splitting..."
			while [ "${#array_connected_nodes[@]}" -lt "${#array_active_nodes[@]}" ] && [ "$attempts" -lt 10 ];
			do
				copy_array array_topology_node_pairs_backup array_topology_node_pairs
				copy_array array_topology_node_pairs_stopped_backup array_topology_node_pairs_stopped
				create_random_network_topology ${array_all_nodes[@]}
				unset array_topology_node_pairs_stopped
				check_stopping_nodes_cause_network_split "${array_stopped_nodes[@]}"
				((attempts++))
			done
			if [ "$attempts" == 10 ];
			then
				copy_array array_topology_node_pairs_backup array_topology_node_pairs
				copy_array array_topology_node_pairs_stopped_backup array_topology_node_pairs_stopped
			else
				disconnect_network "${array_topology_node_pairs_backup[@]}"
				connect_network "${array_topology_node_pairs[@]}"
				echo "Created topology that has ${#array_topology_node_pairs[@]} connections out of max connections ${#array_network_connection_pool[@]} in the network"
			fi
			echo array topoloy node pairs is "${array_topology_node_pairs[@]}" with index "${!array_topology_node_pairs[@]}"
			echo array topology node pairs stopped is "${array_topology_node_pairs_stopped[@]}" with index "${!array_topology_node_pairs_stopped[@]}"
		fi
	fi

	if [ "$bool_sync_new_node" == 1 ];
	then
		dice=$( bc <<< "$RANDOM % 10" )
		if [ "$dice" == 0 ];
		then
			echo Initializing a new node to test syncing from genesis...
			shuffle_array "${array_active_nodes[@]}"
			node=${shuffled_array[0]}
			((node_count++))
			array_topology_node_pairs+=("$node_count $node")
			#echo "array topology node pairs is now ${array_topology_node_pairs[@]}"
			initialize_node $node_count
			echo Waiting 30 sec for stockd...
			sleep 30
			connect_network "${array_topology_node_pairs[@]}"
			sleep 30
			wait_until_sync "${array_active_nodes[@]}"
			check_dotstock_hash "${array_active_nodes[@]}"
			echo Removing test sync node...
			stop_node $node_count
			sleep 30
			remove_node $node_count
			for i in ${!array_topology_node_pairs[@]};
			do
				unset connection
				connection=(${array_topology_node_pairs[$i]})
				if [ "$node_count" == "${connection[0]}" ] || [ "$node_count" == "${connection[1]}" ];
				then
					unset array_topology_node_pairs[$i]
				fi
			done
			((node_count--))
		fi
	fi
	cycles_left=$(bc <<< "$wait_until_cycle - $this_cycle")
	previous_block=$current_block
	current_block=$blocks
	shuffle_array "${array_stressing_nodes[@]}"
	node=${shuffled_array[0]}
	echo Current block: $current_block Current cycle: $this_cycle - $cycles_left cycle\(s\) left to finish.
	echo Tokens and NFTs created: $(stock_cli $node listtokens  | jq ".[].name" |  wc -l)
	echo Active nodes: ${array_active_nodes[@]} Inactive nodes: ${array_stopped_nodes[@]}
#	for j in $(seq 0 1 $( bc <<< "$node_count-1"));
#	do
#		echo Reorganizations Node $j: $(prev=0;for i in $(grep height= ${array_data[$j]}/devnet/debug.log|sed -n -e '/height\='$previous_block'/,$p'|sed -n 's/.*height\=\([0-9]*\) .*/\1/p'); do if [ $i -lt $prev ]; then echo $i; fi; prev=$i; done)
#	done
done

##############################Network Splitting
if [ "$bool_network_split" == 1 ];
then
	echo Stopping and then starting all nodes...
	for i in ${array_active_nodes[@]};
	do
		echo Stopping node $i...
		stop_node $i
	done
	sleep 10
	for i in ${array_all_nodes[@]};
	do
		echo Starting node $i...
		start_node $i
	done
	echo Waiting 30 sec for stockd...
	sleep 30
	echo Splitting the network into $network_count sub networks...
	network_split_started=1
	counter=0
	for i in ${array_stressing_nodes[@]};
	do
		if [ "$counter" == "$network_count" ];
		then
			counter=0
		fi
		eval "array_stressing_nodes_network$counter[$i]=$i"
		eval "array_all_nodes_network$counter[$i]=$i"
		((counter++))
	done
	counter=0
	for i in ${array_all_nodes[@]};
	do
		if [ "$counter" == "$network_count" ];
		then
			counter=0
		fi
		match=0
		for j in ${array_stressing_nodes[@]};
		do
			if [ "$j" == "$i" ];
			then
				match=1
			fi
		done
		if [ "$match" != 1 ];
		then
			eval "array_all_nodes_network$counter[$i]=$i"
		fi
		((counter++))
	done
	copy_array array_topology_node_pairs array_topology_node_pairs_backup
	for nc in $(seq 0 1 $( bc <<< "$network_count-1" ));
	do
		eval "echo stressing nodes in network $nc: \${array_stressing_nodes_network$nc[@]}"
		eval "echo nodes in network $nc: \${array_all_nodes_network$nc[@]} index: \${!array_all_nodes_network$nc[@]}"
		eval "create_random_network_topology \"\${array_all_nodes_network$nc[@]}\""
		copy_array array_topology_node_pairs array_topology_node_pairs_network$nc
		eval "echo topology of network$nc is: \"\${array_topology_node_pairs_network$nc[@]}\""
		eval "connect_network \"\${array_topology_node_pairs_network$nc[@]}\""
	done
	wait_until_cycle=$(bc <<< "$this_cycle + $cycles_network_split")
	while [ $wait_until_cycle -gt $this_cycle ]; do
		echo ''
		echo Stressing for $seconds_round seconds...
		stress $seconds_round
		for nc in $(seq 0 1 $( bc  <<< "$network_count - 1" ));
		do
			echo Waiting until nodes are synced...
			eval "wait_until_sync \"\${array_all_nodes_network$nc[@]}\""
			echo Checking token hashes match...
			eval "check_dotstock_hash \"\${array_all_nodes_network$nc[@]}\""
			eval "shuffle_array \"\${array_all_nodes_network$nc[@]}\""
			node=${shuffled_array[0]}
			check_consensus_parameters $node
			blocks=$(stock_cli $node getinfo|jq .blocks)
			echo Tokens and NFTs created: $(stock_cli $node listtokens  | jq ".[].name" |  wc -l)
		done
		cycles_left=$(bc <<< "$wait_until_cycle - $this_cycle")
		previous_block=$current_block
		current_block=$blocks
		shuffle_array "${array_stressing_nodes[@]}"
		node=${shuffled_array[0]}
		echo Current block: $current_block
		echo Current cycle: $this_cycle - $cycles_left cycle\(s\) left to finish.
		for i in ${array_stressing_nodes[@]};
		do
			out=$(stock_cli $i "staking false")
			echo "Node $i balance: $(stock_cli $i getwalletinfo | jq '.private_balance') x0DYNS"
		done
		done

	create_random_network_topology "${array_all_nodes[@]}"
	connect_network "${array_topology_node_pairs[@]}"
	wait_until_sync "${array_all_nodes[@]}"
	check_dotstock_hash "${array_all_nodes[@]}"
fi
#############################


extra_blocks=$(bc <<< "($RANDOM%$extra_blocks_to_stake_randomness)+$extra_blocks_to_stake")
blocks=$(stock_cli $node getinfo|jq .blocks)
wait_until=$(bc <<< "$blocks+$extra_blocks")
echo ''
echo Stopping stresser. Waiting until $extra_blocks blocks are staked
for i in ${array_stressing_nodes[@]};
do
	out=$(stock_cli $i "staking true")
done
while [ $blocks -lt $wait_until ];
do
	sleep 30
	blocks=$(stock_cli $node getinfo|jq .blocks)
	echo $(bc <<< "$wait_until-$blocks") blocks left...
done
for i in ${array_stressing_nodes[@]};
do
        out=$(stock_cli $i "staking false")
done
echo ''
echo Waiting until all nodes are synced
wait_until_sync "${array_active_nodes[@]}"
echo Ok! Block: $(stock_cli $node getinfo|jq .blocks) Cycle: $(bc <<< $(stock_cli $node getinfo|jq .blocks)/$voting_cycle_length).
echo Tokens and NFTs created: $(stock_cli $node listtokens  | jq ".[].name" |  wc -l)

echo ''
echo Running verifychain test with node ${verifychain_nodes[@]}...
echo ''

starting_block=0
for i in ${!array_verifychain_nodes[@]};
do
	((starting_block++))
	check_node $starting_block ${array_verifychain_nodes[$i]} &
done

wait

for n in ${array_stressing_nodes[@]};
do
        echo "Node $n balance: $(stock_cli $n getbalance) t0DYNS"
done


echo "script finished successfully"
terminate 0
