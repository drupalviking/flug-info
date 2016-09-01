<?php
/**
 * Created by PhpStorm.
 * User: drupalviking
 * Date: 09/08/16
 * Time: 10:35
 */
function fluginfo_drush_command() {
  $items['fluginfo-temp'] = array(
    'aliases' => array('ftempo'),
  );

  $items['fluginfo-fetch-international-flights'] = [
    'aliases' => ['finternat'],
  ];

  $items['fluginfo-fetch-airiceland-flights'] = [
    'aliases' => ['fairiceland'],
  ];

  return $items;
}

/**
 * If you need a platform to run certain commands, you can use this function
 * @return array|bool
 */
function drush_fluginfo_temp(){

}

/**
 * Fetches two json feeds from apis.is  The departure and arrival feeds.
 * Fetches the node for Kef airport and sends the data for processing into the
 * node
 *
 * @return array|null|\stdClass
 */
function drush_fluginfo_fetch_international_flights(){
  $string = variable_get('fluginfo_block_international_service') .
    '?language=' . variable_get('fluginfo_block_international_locale');

  $departures_json = file_get_contents($string . "&type=departures");
  $arrivals_json = file_get_contents($string . "&type=arrivals");
  $node = _fluginfo_get_airport_info('KEF');

  if(!$node){
    $node = _fluginfo_create_flight_node($departures_json, $arrivals_json, 'Keflavík ', 'KEF');
  }
  else{
    $node = _fluginfo_update_flight_node($departures_json, $arrivals_json, $node);
  }

  return $node;
}

/**
 * Fetches two XML feeds from Air Iceland (departure and arrivals).  Encodes them
 * into json and sends them for further processing.  Finally it adds the data
 * to Flugvallauppýsingar node.
 *
 * Config to the request strings is under /admin/config/services/flightinfo
 */
function drush_fluginfo_fetch_airiceland_flights(){
  $arrival_string = variable_get('fluginfo_block_airiceland_arrivals_service') .
    '?RequestType=arrivals&Arrival=' . variable_get('fluginfo_block_airiceland_arrival_airports') .
    '&GapBefore=' . variable_get('fluginfo_block_airiceland_gap_before') .
    '&GapAfter=' . variable_get('fluginfo_block_airiceland_gap_after') .
    '&locale=' . variable_get('fluginfo_block_airiceland_locale');
  $departure_string = variable_get('fluginfo_block_airiceland_departure_service') .
    '?RequestType=departures&Departure=' . variable_get('fluginfo_block_airiceland_departure_airports') .
    '&GapBefore=' . variable_get('fluginfo_block_airiceland_gap_before') .
    '&GapAfter=' . variable_get('fluginfo_block_airiceland_gap_after') .
    '&locale=' . variable_get('fluginfo_block_airiceland_locale');

  $arrival_xml = simplexml_load_file($arrival_string);
  $departure_xml = simplexml_load_file($departure_string);

  $arrival_json = json_decode(json_encode($arrival_xml));
  $departure_json = json_decode(json_encode($departure_xml));

  $arrivals_array = _fluginfo_process_airiceland_json($arrival_json, 'arrival');
  $departures_array = _fluginfo_process_airiceland_json($departure_json, 'departure');

  _fluginfo_add_airiceland_to_node($arrivals_array, $departures_array);
}

/**
 * Extracts data from Air Iceland and formats it again, to look like the data
 * we get from apis.is (for Kef Airport).
 *
 * @param string $json
 * @param string $arr_dep with either arrivals or departure as value
 * @return array $lump_array Which contains all data for 8 available airports in
 * Iceland
 */
function _fluginfo_process_airiceland_json($json, $arr_dep){
  $lump_array = [];
  foreach($json->destination as $destination){
    $dest_string = _fluginfo_extract_destination($destination->header_text, $arr_dep);
    $dest_array = [];

    if(isset($destination->flights->flight)){
      //If there is only one flight, we will get an object instead of an array.
      //Therefor we have to change the object into an array, so the rest of the
      //logic works.
      if(is_object($destination->flights->flight)){
        $flight_obj = $destination->flights->flight;
        $destination->flights->flight = [];
        $destination->flights->flight[] = $flight_obj;
      }
      foreach($destination->flights->flight as $flight){
        $flight_info = new stdClass();
        $flight_info->flightNumber = $flight->flight_number;
        $flight_info->airline = _fluginfo_extract_airline($flight->flight_number);
        if($arr_dep == 'arrival'){
          $time = strtotime($flight->arrival->scheduled);
          $flight_info->date = strftime('%d.%m', $time);
          $flight_info->from = $flight->departure->airport;
          if($flight->arrival->estimate != 'N/A'){
            $flight_info->plannedArrival = $flight->arrival->estimate;
          }
          else{
            $scheduled_time = substr($flight->arrival->scheduled, -5);
            $flight_info->plannedArrival = $scheduled_time;
          }
          if($flight->arrival->actual != 'N/A'){
            $flight_info->realArrival = $flight->arrival->actual;
          }
          else{
            $flight_info->realArrival = null;
          }
        }
        else{
          $time = strtotime($flight->departure->scheduled);
          $flight_info->date = strftime('%d.%m', $time);
          $flight_info->to = $flight->arrival->airport;

          if($flight->departure->estimate != 'N/A'){
            $flight_info->plannedArrival = $flight->departure->estimate;
          }
          else{
            $scheduled_time = substr($flight->departure->scheduled, -5);
            $flight_info->plannedArrival = $scheduled_time;
          }
          if($flight->departure->actual != 'N/A'){
            $flight_info->realArrival = $flight->departure->actual;
          }
          else{
            $flight_info->realArrival = null;
          }
        }

        $dest_array['results'][] = $flight_info;
      }

      $json = json_encode($dest_array);
      $lump_array[$dest_string->airport][$arr_dep] = $json;
      $lump_array[$dest_string->airport]['town'] = $dest_string->town;
    }
    else{
      $lump_array[$dest_string->airport][$arr_dep] = null;
      $lump_array[$dest_string->airport]['town'] = $dest_string->town;
    }
  }

  return $lump_array;
}

/**
 * Adds the data extracted from Air Iceland feed into the Flugupplýsingar-node
 *
 * @param $arrivals_array
 * @param $departures_array
 */
function _fluginfo_add_airiceland_to_node($arrivals_array, $departures_array){
  foreach($arrivals_array as $key => $value){
    $node = _fluginfo_get_airport_info($key);

    if(!$node){
      _fluginfo_create_flight_node($departures_array[$key]['departure'], $value['arrival'], $value['town'], $key);
    }
    else{
      _fluginfo_update_flight_node($departures_array[$key]['departure'], $value['arrival'], $node);
    }
  }
}

/**
 * Returns the airline name, determined from the flight number
 *
 * @param $flight_number
 * @return string
 */
function _fluginfo_extract_airline($flight_number){
  $airline_code = substr($flight_number, 0, 2);
  switch($airline_code){
    case 'NY' :
      return 'Air Iceland';
      break;
    case 'FN' :
      return 'Norlandair';
      break;
    case 'RC' :
      return 'Atlantic Airways';
      break;
    default :
      return 'Unknown';
  }
}

/**
 * Extracts the name of town and the airport code from a string
 *
 * @param $text
 * @param $arr_dep
 * @return \stdClass
 */
function _fluginfo_extract_destination($text, $arr_dep){
  $open_bracet_loc = strpos($text, '(');
  $start_index = ($arr_dep == 'arrival') ? 7 : 13;
  $location = new stdClass();
  $location->town = trim(substr($text, $start_index, $open_bracet_loc - $start_index));
  $location->airport = substr($text, $open_bracet_loc + 1, 3);

  return $location;
}

/**
 * Creates the actual Flugupplýsingar node, from data
 *
 * @param $departures_json
 * @param $arrivals_json
 * @param $airport_name
 * @param $airport_code
 * @return \stdClass
 * @throws \Exception
 */
function _fluginfo_create_flight_node($departures_json, $arrivals_json, $airport_name, $airport_code){
  $node = new stdClass();
  $node->title = $airport_name;
  $node->type = 'flugupplysingar';
  node_object_prepare($node);

  $node->language = 'und';
  $node->uid = 1;

  $node->field_flugvollur[LANGUAGE_NONE][0] = [
    'value' => $airport_code,
    'format' => null,
    'safe_value' => htmlentities($airport_code),
  ];

  $node->field_flugtimar_fra[LANGUAGE_NONE][0] = [
    'value' => time(),
    'timezone' => 'Atlantic/Reykjavik',
    'timezone_db' => 'UTC',
    'date_type' => 'datestamp',
  ];

  if($departures_json){
    $node->field_brottfararupplysingar[LANGUAGE_NONE][0] = [
      'value' => $departures_json,
      'format' => null,
      'safe_value' => htmlentities($departures_json),
    ];
  }
  $node->field_gildur_brottftimi[LANGUAGE_NONE][0]['value'] = ($departures_json) ? 1 : 0;

  if($arrivals_json){
    $node->field_komuupplysingar[LANGUAGE_NONE][0] = [
      'value' => $arrivals_json,
      'format' => null,
      'safe_value' => htmlentities($arrivals_json),
    ];
  }
  $node->field_gildur_komutimi[LANGUAGE_NONE][0]['value'] = ($departures_json) ? 1 : 0;

  $node = node_submit($node);
  node_save( $node );

  return $node;
}

/**
 * Updates a Flugupplýsingar node
 *
 * @param $departures_json
 * @param $arrivals_json
 * @param $node
 * @return mixed
 * @throws \Exception
 */
function _fluginfo_update_flight_node($departures_json, $arrivals_json, $node){
  $node->field_flugtimar_fra[LANGUAGE_NONE][0]['value'] = time();

  if($departures_json){
    $node->field_brottfararupplysingar[LANGUAGE_NONE][0] = [
      'value' => $departures_json,
      'format' => null,
      'safe_value' => htmlentities($departures_json),
    ];
  }
  $node->field_gildur_brottftimi[LANGUAGE_NONE][0]['value'] = ($departures_json) ? 1 : 0;

  if($arrivals_json){
    $node->field_komuupplysingar[LANGUAGE_NONE][0] = [
      'value' => $arrivals_json,
      'format' => null,
      'safe_value' => htmlentities($arrivals_json),
    ];
  }
  $node->field_gildur_komutimi[LANGUAGE_NONE][0]['value'] = ($departures_json) ? 1 : 0;

  $node = node_submit($node);
  node_save( $node );

  return $node;
}